#!/usr/bin/env python3
"""gp5tool: check and flash Valeton GP-5 firmware images over USB. No debug probe.

    gp5tool.py check  <image.bin>                 what is in the image, and is it safe to send
    gp5tool.py status                             is a GP-5 connected, and in which mode
    gp5tool.py flash  <image.bin> --sections app  flash one or more sections (see below)
    gp5tool.py combine <base.bin> --take dsp=<a.bin> --take app=<b.bin> -o <out.bin>
                                                  one image from several: <base> with the named sections
                                                  taken from other images (a firmware release = one file)

Sections (an image always holds all five; you choose which ones are written):
    app       0x62  control processor firmware (screen, knobs, USB)
    dsp       0x63  audio processor firmware
    dsp-data  0x67  audio processor data
    models    0x66  amp / cabinet model data
    presets   0x65  factory presets and catalog
    all             everything, in the image's own order (a full stock restore)

Works with stock Valeton images and with images built by this project (platform/*/build tools).
When you change both the app and the DSP firmware, flash the app first (or both from one image in one
run): Valeton's app shows a dark screen with a DSP firmware it does not know.
The image is checked completely before anything is sent. The pedal's factory loader cannot be
overwritten over USB, so a failed or wrong flash is recoverable: power the pedal on while holding
the foot switch and the PARA knob, then flash again.

Needs Python 3.9+, `pip install mido python-rtmidi`. Protocol and container code: gp5_proto.py, htfw.py.
"""
import argparse, re, struct, sys, time

import htfw
from gp5_proto import (CLASS_CMD, build_sysex, parse_sysex, build_update_stream, update_ack_status)

SECTIONS = {"app": 0x62, "dsp": 0x63, "dsp-data": 0x67, "models": 0x66, "presets": 0x65}
NAMES = {v: k for k, v in SECTIONS.items()}
# Every section the pedal knows, in container order, with its flash address. An image holds all of them
# (Valeton's) or some of them in this order (this project's: usually just dsp and app).
LAYOUT = [(0x63, 0x200000), (0x67, 0x240000), (0x66, 0x280000), (0x62, 0x000000), (0x65, 0x190000)]
# Largest section the pedal's loaders accept (docs/platform/protocols.md).
MAX_LEN = {0x62: 0x168000, 0x63: 0x10000, 0x67: 0x100000, 0x66: 0x43000}
# Sections of the stock V1.0.6 image (GP-5_USB_V1.0.6_20250626.bin): stored CRC and length.
STOCK_V106 = {0x63: (0x3688, 0xC348), 0x67: (0x27A5, 0x55028), 0x66: (0x2010, 0x42FE4),
              0x62: (0x985C, None), 0x65: (0xC73C, 0x2D000)}

MODE_OPEN, MODE_STOCK, MODE_LOADER = "open firmware", "stock firmware", "update mode"
# A GP-5 port that does not answer the state query. Seen on hardware when Valeton's app runs with a DSP
# firmware it does not know: its screen stays dark, but it still obeys the restart-into-update command.
MODE_SILENT = "firmware that does not answer"
OPEN_PORT, ANY_PORT = "GP-5 Open FW", "GP-5"
RECOVERY = ("Recovery: unplug the pedal, hold the foot switch and the PARA knob, plug it back in (the screen stays\n"
            "dark = update mode), then run the flash again. The factory loader is never overwritten.")


class Problem(Exception):
    pass


# ---------------------------------------------------------------- image checks (no device)

def check_app(data):
    """What the factory loader checks before it starts the app (check_user_code), plus the size limit."""
    errs = []
    if len(data) < 0x108: return ["app section is too short to hold a vector table"]
    bad = [o for o in range(0, 0xA4, 4) if data[o] != 0x48]
    if bad: errs.append("app vector table: entry at 0x%x is not a jump" % bad[0])
    if struct.unpack_from("<I", data, 0xC0)[0] != 0xB0BEBDC9: errs.append("app magic at 0xC0 is missing")
    for o in range(0, 0xA4, 4):
        w = int.from_bytes(data[o:o + 4], "big")
        if w >> 24 == 0x48 and not 0x100 <= o + ((w & 0xFFFFFF) << 1) < len(data):
            errs.append("app vector at 0x%x jumps outside the image" % o); break
    return errs


def check_dsp(data):
    """The DSP resident's boot contract: header word 3 = vector table address, then {stack, reset}."""
    errs = []
    if len(data) < 0x408: return ["DSP section is too short"]
    vt = struct.unpack_from("<I", data, 0x0C)[0]
    if not 0x4000 <= vt < 0x4000 + len(data) - 8: return ["DSP header: vector table address 0x%x is outside the section" % vt]
    sp, reset = struct.unpack_from("<2I", data, vt - 0x4000)
    if not (0x20000000 < sp <= 0x20008000 or 0x10000000 < sp <= 0x10020000):
        errs.append("DSP initial stack pointer 0x%08x is not in RAM" % sp)
    if not (reset & 1) or not 0x4000 <= (reset & ~1) < 0x4000 + len(data):
        errs.append("DSP reset vector 0x%08x is not Thumb code inside the section" % reset)
    return errs


def load_sections(blob):
    """Container, product, section map and every CRC. Returns [(type, data)] in image order."""
    h = htfw.parse(blob)
    if h["product"] != "GP-5" or h["comp_flag"] or h["size_field"] != len(blob):
        raise ValueError("expected an uncompressed GP-5 HTFW image")
    order = [x for x, _ in LAYOUT]
    got = [(e["type"], e["flash"]) for e in h["entries"]]
    if not got or any(g not in LAYOUT for g in got) or [order.index(t) for t, _ in got] != sorted(
            set(order.index(t) for t, _ in got)):
        raise ValueError("unexpected section types, order or addresses")
    sections, cursor = [], 0
    for e in h["entries"]:
        start = h["payload_base"] + e["doff"]
        data = blob[start:start + e["dlen"]]
        if e["doff"] != cursor or not data or len(data) != e["dlen"] or htfw.stored_crc(data) != e["crc"]:
            raise ValueError("section 0x%02x: offset, length or CRC is wrong" % e["type"])
        sections.append((e["type"], data))
        cursor += len(data)
    if h["payload_base"] + cursor != len(blob):
        raise ValueError("trailing or missing data")
    return sections


def make_image(sections):
    """A new image from {type: data}, no base image needed. Header fields as Valeton's tools write them,
    version V0.0.0 (shown by Valeton's updater only)."""
    payload, table = bytearray(), bytearray()
    for t, flash in LAYOUT:
        if t not in sections: continue
        data = sections[t]
        table += struct.pack("<HBBIII", htfw.stored_crc(data), 0, t, flash, len(payload), len(data))
        payload += data
    header = bytearray(htfw.HEADER_LEN)
    struct.pack_into("<IHHI", header, 0, htfw.MAGIC, 0, 1, htfw.HEADER_LEN + len(table) + len(payload))
    header[0x0C:0x10] = b"GP-5"
    header[0x1C:0x20] = b"V\x00\x00\x00"
    header[0x22] = len(table) // htfw.ENTRY_LEN
    struct.pack_into("<I", header, 0x24, len(payload))
    out = bytes(header) + bytes(table) + bytes(payload)
    inspect(out)
    return out


def inspect(blob):
    """Validate an image. Returns (sections [(type, data)], report lines). Raises Problem."""
    try:
        sections = load_sections(blob)
    except Exception as e:
        raise Problem("not a valid GP-5 firmware image: %s" % e)
    lines, errs = [], []
    for t, d in sections:
        crc = htfw.stored_crc(d)
        stock = STOCK_V106.get(t)
        kind = "stock V1.0.6" if stock and stock[0] == crc and stock[1] in (None, len(d)) else "not stock V1.0.6"
        lines.append("  %-9s 0x%02x  %8d bytes  %s" % (NAMES[t], t, len(d), kind))
        if t in MAX_LEN and len(d) > MAX_LEN[t]:
            errs.append("%s section is larger than the pedal accepts (%d > %d)" % (NAMES[t], len(d), MAX_LEN[t]))
        if t == 0x62: errs += check_app(d)
        if t == 0x63: errs += check_dsp(d)
    if errs: raise Problem("image would not boot:\n  " + "\n  ".join(errs))
    return sections, lines


def pick(sections, names):
    want = []
    for n in names:
        for part in n.split(","):
            part = part.strip().lower()
            if part == "all": want += [t for t, _ in sections]
            elif part in SECTIONS: want.append(SECTIONS[part])
            elif part: raise Problem("unknown section '%s' (choose from: %s, all)" % (part, ", ".join(SECTIONS)))
    if not want: raise Problem("no section chosen; use --sections, for example --sections app")
    return [(t, d) for t, d in sections if t in want]       # always in the image's order


def combine(base, takes):
    """`base` image with sections replaced: takes = {section type: data}. Returns the new image, which is
    validated like any other before it is returned."""
    h = htfw.parse(base)
    payload, table = bytearray(), bytearray()
    for e in h["entries"]:
        start = h["payload_base"] + e["doff"]
        data = takes.get(e["type"], base[start:start + e["dlen"]])
        table += struct.pack("<HBBIII", htfw.stored_crc(data), e["pad"], e["type"], e["flash"], len(payload), len(data))
        payload += data
    header = bytearray(h["header"])
    struct.pack_into("<I", header, 0x08, len(header) + len(table) + len(payload))
    struct.pack_into("<I", header, 0x24, len(payload))
    out = bytes(header) + bytes(table) + bytes(payload)
    inspect(out)
    return out


# ---------------------------------------------------------------- pedal link

class Timing:
    """Waits used during an update. Tests shrink them."""
    reply = 1.0          # answer to a state query
    ack = 5.0            # one data frame
    begin = 30.0         # BEGIN: the pedal erases the section first
    begin_settle = 10.0
    settle = 1.5
    final = 8.0          # END_IMAGE status
    reenum = 25.0        # the pedal re-appearing on USB
    retries = 6
    retry_delay = 0.05
    poll = 0.15


def port_base(name):
    """The device part of a MIDI port name, the same for a device's input and output on every system:
    Linux 'GP-5:GP-5 MIDI 1 16:0', macOS 'GP-5 Open FW', Windows 'GP-5 Open FW 2' (output) / '... 1' (input)."""
    return re.sub(r"\s+\d+$", "", name.split(":")[0]).strip()


class MidiPorts:
    """Real USB-MIDI through mido."""

    def __init__(self):
        try:
            import mido
        except ImportError:
            raise Problem("the 'mido' package is missing: pip install mido python-rtmidi")
        self.mido = mido

    def names(self):
        outs, ins = self.mido.get_output_names(), self.mido.get_input_names()
        bases = {port_base(i) for i in ins}
        return [n for n in outs if port_base(n) in bases]

    def open(self, name):
        ins = self.mido.get_input_names()
        iname = name if name in ins else next(i for i in ins if port_base(i) == port_base(name))
        return MidiLink(self.mido, self.mido.open_output(name), self.mido.open_input(iname))


class MidiLink:
    def __init__(self, mido, out, inp):
        self.mido, self.out, self.inp = mido, out, inp

    def send(self, raw):
        self.out.send(self.mido.Message.from_bytes(list(raw)))

    def recv(self, timeout):
        end = time.monotonic() + timeout
        while True:
            for m in self.inp.iter_pending():
                if m.type == "sysex": return bytes([0xF0]) + bytes(m.data) + bytes([0xF7])
            if time.monotonic() >= end: return None
            time.sleep(0.0005)

    def flush(self):
        for _ in self.inp.iter_pending(): pass

    def close(self):
        self.out.close(); self.inp.close()


def payload_of(raw):
    try:
        _, _, _, pl, ok = parse_sysex(raw)
        return bytes(pl) if ok else None
    except (ValueError, IndexError, AssertionError):
        return None


def ask(link, payload, want, timeout):
    """Send a command frame, return the first valid reply payload that starts with `want`."""
    link.flush()
    link.send(build_sysex(CLASS_CMD, 0, payload))
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        raw = link.recv(max(0.0, end - time.monotonic()))
        if raw is None: break
        pl = payload_of(raw)
        if pl is not None and pl[:len(want)] == bytes(want): return pl
    return None


def find(ports):
    """Return (port name, mode) of the connected pedal, or (None, None)."""
    names = ports.names()
    name = next((n for n in names if OPEN_PORT in n), None)
    if name: return name, MODE_OPEN
    name = next((n for n in names if ANY_PORT in n), None)
    if not name: return None, None
    # While the pedal restarts, a port can still be listed and fail on open or send (Windows raises
    # "error sending sysex message"). That is "not there yet", not an error.
    try:
        link = ports.open(name)
    except Exception:
        return None, None
    try:
        pl = ask(link, [0x12, 0x30], [0x11, 0x30], Timing.reply)
    except Exception:
        return None, None
    finally:
        try: link.close()
        except Exception: pass
    if pl is not None and len(pl) >= 4 and pl[3] == 1: return name, MODE_LOADER
    if pl is not None: return name, MODE_STOCK
    return name, MODE_SILENT


def wait_for(ports, want_modes, timeout, say):
    end = time.monotonic() + timeout
    last = (None, None)
    while time.monotonic() < end:
        last = find(ports)
        if last[1] in want_modes: return last
        time.sleep(Timing.poll)
    return last


def to_loader(ports, say):
    """Bring the pedal into update mode from whatever it is running. Returns the loader's port name."""
    name, mode = find(ports)
    if name is None:
        raise Problem("no GP-5 found on USB. Connect it and switch it on, or see `status`.")
    if mode == MODE_LOADER:
        say("pedal is already in update mode"); return name
    say("pedal is running the %s; asking it to restart into update mode" % mode)
    ack = None
    try:
        link = ports.open(name)
        try:
            ack = ask(link, [0x11, 0x6F], [0x14, 0x08], Timing.reply)
        finally:
            link.close()
    except Problem:
        raise
    except Exception:
        pass                                   # the port went away under us: the pedal is already restarting
    if ack is None: say("  (no acknowledgement; waiting for the restart anyway)")
    name, mode = wait_for(ports, (MODE_LOADER,), Timing.reenum, say)
    if mode != MODE_LOADER:
        raise Problem("the pedal did not go into update mode%s.\n%s" %
                      (" and does not answer" if mode == MODE_SILENT else "", RECOVERY))
    return name


def send_acked(link, raw, timeout):
    """One frame, wait for the update status. Returns 0 = acknowledged, other = NAK code, None = silence."""
    link.flush()
    link.send(raw)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        got = link.recv(max(0.0, end - time.monotonic()))
        if got is None: return None
        pl = payload_of(got)
        st = update_ack_status(pl) if pl is not None else None
        if st is not None: return st
    return None


def send_reliably(link, raw, timeout, what):
    for attempt in range(Timing.retries + 1):
        if attempt: time.sleep(Timing.retry_delay)
        st = send_acked(link, raw, timeout)
        if st == 0: return attempt
    raise Problem("the pedal stopped acknowledging at %s (%s)." %
                  (what, "no answer" if st is None else "error code %02x" % st))


def flash(ports, sections, say, progress=None):
    """Send the chosen sections. Returns the mode the pedal is in afterwards (update mode if it stayed
    there, None if it did not re-appear)."""
    name = to_loader(ports, say)
    link = ports.open(name)
    started = False
    try:
        if ask(link, [0x12, 0x30], [0x11, 0x30], Timing.reply) is None:
            raise Problem("the loader does not answer on %s." % name)
        send_reliably(link, build_sysex(CLASS_CMD, 0, [0x11, 0x60]), Timing.ack, "the start of the update")
        time.sleep(Timing.settle)
        for t, data in sections:
            stream = build_update_stream(data, t)         # ENTER, BEGIN, DATA..., END_SEC, END_IMAGE
            body = stream[1:-1]
            total = len(body) - 2
            say("%s (0x%02x): erasing, then %d bytes in %d packets" % (NAMES[t], t, len(data), total))
            try:
                send_reliably(link, body[0][1], Timing.begin, "the erase of the %s section" % NAMES[t])
            except Problem as e:
                if not started:
                    raise Problem(str(e) + "\nNothing was written. If this happens every time, the audio processor's "
                                  "updater is stalling\n(a known fault on at least one unit; see 'Known limits' in GETTING-STARTED.md).")
                raise
            started = True
            time.sleep(Timing.begin_settle)
            retried = 0
            for i, (label, raw) in enumerate(body[1:-1]):
                retried += send_reliably(link, raw, Timing.ack, "%s packet %d of %d" % (NAMES[t], i + 1, total))
                if progress: progress(NAMES[t], i + 1, total)
            send_reliably(link, body[-1][1], Timing.ack, "the end of the %s section" % NAMES[t])
            say("%s: written%s" % (NAMES[t], " (%d packets repeated)" % retried if retried else ""))
            time.sleep(Timing.settle)
        st = send_acked(link, build_sysex(CLASS_CMD, 0, [0x11, 0x6E, 0x6E, 0x00]), Timing.final)
        if st not in (0, None):
            raise Problem("the pedal refused to finish the update (error code %02x)." % st)
        say("update finished" + ("" if st == 0 else " (no final status; the pedal may already be restarting)"))
    except Problem as e:
        if started:
            raise Problem(str(e) + "\nThe update is INCOMPLETE: the pedal will stay in update mode until a flash "
                          "completes.\n" + RECOVERY)
        raise
    finally:
        link.close()
    name, mode = wait_for(ports, (MODE_OPEN, MODE_STOCK), Timing.reenum, say)
    return mode if name else None


# ---------------------------------------------------------------- commands

def cmd_check(a):
    blob = open(a.image, "rb").read()
    sections, lines = inspect(blob)
    print("%s: valid GP-5 firmware image, %d bytes" % (a.image, len(blob)))
    print("\n".join(lines))
    return 0


def cmd_combine(a):
    base = open(a.base, "rb").read()
    inspect(base)
    takes = {}
    for item in a.take or []:
        name, _, path = item.partition("=")
        name = name.strip().lower()
        if name not in SECTIONS or not path: raise Problem("--take wants NAME=IMAGE with NAME one of: %s" % ", ".join(SECTIONS))
        if SECTIONS[name] in takes: raise Problem("section '%s' given twice" % name)
        sections, _ = inspect(open(path, "rb").read())
        takes[SECTIONS[name]] = dict(sections)[SECTIONS[name]]
    if not takes: raise Problem("nothing to take; use --take dsp=IMAGE and/or --take app=IMAGE")
    out = combine(base, takes)
    open(a.out, "wb").write(out)
    print("%s: %d bytes" % (a.out, len(out)))
    print("\n".join(inspect(out)[1]))
    print("Flash it in one run with:  gp5tool.py flash %s --sections %s" % (a.out, ",".join(NAMES[t] for t in takes)))
    return 0


def section_from(name, path):
    """A section's bytes from a raw section file or from the same section of an image."""
    if name not in SECTIONS or not path: raise Problem("want NAME=FILE with NAME one of: %s" % ", ".join(SECTIONS))
    raw = open(path, "rb").read()
    if raw[:4] == b"HTFW":
        sections, _ = inspect(raw)
        if SECTIONS[name] not in dict(sections): raise Problem("%s has no %s section" % (path, name))
        return dict(sections)[SECTIONS[name]]
    return raw


def cmd_pack(a):
    takes = {}
    for item in a.section:
        name, _, path = item.partition("=")
        name = name.strip().lower()
        if name in SECTIONS and SECTIONS[name] in takes: raise Problem("section '%s' given twice" % name)
        takes[SECTIONS.get(name, -1)] = section_from(name, path)
    out = make_image(takes)
    open(a.out, "wb").write(out)
    print("%s: %d bytes" % (a.out, len(out)))
    print("\n".join(inspect(out)[1]))
    print("Flash it with:  gp5tool.py flash %s --sections all" % a.out)
    return 0


def cmd_status(a):
    name, mode = find(MidiPorts())
    if name is None: print("No GP-5 found on USB."); return 1
    print("GP-5 on '%s': %s" % (name, mode))
    if mode == MODE_SILENT:
        print("This is normal when the app and the audio firmware on the pedal do not belong together (for example\n"
              "Valeton's app with a custom DSP firmware). `flash` can still put it into update mode.")
    return 0


def cmd_flash(a):
    blob = open(a.image, "rb").read()
    sections, lines = inspect(blob)
    chosen = pick(sections, a.sections or [])
    print("%s: valid GP-5 firmware image" % a.image)
    print("\n".join(lines))
    print("Will write: " + ", ".join(NAMES[t] for t, _ in chosen))
    if not a.yes:
        if input("Keep the pedal connected and powered until this finishes. Type yes to start: ").strip().lower() != "yes":
            print("Nothing sent."); return 1
    t0 = time.monotonic()
    last = [0]

    def progress(name, i, n):
        pct = 100 * i // n
        if pct // 10 != last[0] // 10 or i == n: print("  %s %3d%%" % (name, pct), flush=True)
        last[0] = pct

    mode = flash(MidiPorts(), chosen, lambda s: print(s, flush=True), progress)
    print("Done in %.0f s." % (time.monotonic() - t0))
    if mode in (MODE_OPEN, MODE_STOCK): print("The pedal is back, running the %s." % mode)
    elif mode == MODE_SILENT:
        print("The flash completed and the pedal restarted, but its app does not answer and the screen may stay dark.\n"
              "That is expected when the app and the audio firmware do not belong together: flash the matching\n"
              "other section next (this tool can do it from this state).")
    else: print("The flash completed, but the pedal %s. Unplug it (USB too, if USB powers it), wait a few\n"
                "seconds, and plug it in again without holding any buttons. If it then stays dark:\n%s"
                % ("is still in update mode" if mode == MODE_LOADER else "has not re-appeared yet", RECOVERY))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="validate an image, send nothing"); c.add_argument("image"); c.set_defaults(fn=cmd_check)
    s = sub.add_parser("status", help="find the pedal and report its mode"); s.set_defaults(fn=cmd_status)
    f = sub.add_parser("flash", help="write sections of an image to the pedal"); f.add_argument("image")
    f.add_argument("--sections", action="append", metavar="NAME[,NAME]", help="app, dsp, dsp-data, models, presets, all")
    f.add_argument("--yes", action="store_true", help="do not ask for confirmation"); f.set_defaults(fn=cmd_flash)
    k = sub.add_parser("pack", help="make an image from section files (no base image needed)")
    k.add_argument("section", nargs="+", metavar="NAME=FILE", help="raw section, or an image to take it from")
    k.add_argument("-o", "--out", required=True); k.set_defaults(fn=cmd_pack)
    m = sub.add_parser("combine", help="make one image from sections of several"); m.add_argument("base")
    m.add_argument("--take", action="append", metavar="NAME=IMAGE"); m.add_argument("-o", "--out", required=True)
    m.set_defaults(fn=cmd_combine)
    a = ap.parse_args(argv)
    try:
        return a.fn(a)
    except Problem as e:
        print("ERROR: %s" % e, file=sys.stderr); return 2
    except FileNotFoundError as e:
        print("ERROR: %s" % e, file=sys.stderr); return 2


if __name__ == "__main__":
    sys.exit(main())
