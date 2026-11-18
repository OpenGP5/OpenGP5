#!/usr/bin/env python3
"""Talk to the DSP through the open firmware's link bridge, over USB-MIDI (the pedal must run the open firmware).

  gp5_link.py version              the DSP firmware's version (the platform's DSP answers 0x00010000 = 1.0)
  gp5_link.py stats                link counters: frames, errors, ACKs / NAKs since start-up
  gp5_link.py load                 DSP load: cycles of the last audio block, the worst since start-up, late
                                   blocks (278,000 cycles per 64-sample block is 100 %)
  gp5_link.py raw <hex payload>    send one DSP link payload [cmd u16][len u16][fields], print the reply
  gp5_link.py log [-f]             the log (gp5_log on either processor, and the platform's own lines); -f: keep
                                   following it (Ctrl-C to stop)
  gp5_link.py status [--reset]     load and memory of both processors (1.0); --reset: start the worst values again
  gp5_link.py info                 what the pedal runs: the firmware's name, version and author (OpenGP5 1.0)
  gp5_link.py params               the firmware's parameters (1.0): name, group, value, range
  gp5_link.py set <name|id> <value> set a parameter (a choice by its name too: set SHAPE Ramp)
  gp5_link.py presets              the presets: which hold a sound, their names, the one loaded
  gp5_link.py preset load|save <n>    /    preset name <n> <new name>
  gp5_link.py flash-read <addr> <length> [out.bin]
                                   read the control processor's flash (below 2 MB; reading only) and say
                                   whether it is erased (all FF); with out.bin, save the bytes

Commands of the control processor's USB protocol (platform/nds32/proto.c), SysEx class 1:
  7E 00                      DSP version            -> 7E 00 <ok> <version u32 LE>
  7E 01                      link counters
  7E 03 <off u16> <bytes>    stage up to 48 bytes of a payload
  7E 04 <len u16> <wait/10ms> send the staged payload to the DSP -> 7E 04 <status> <n> <reply>
  7E 05 <addr u32> <count>   read up to 64 bytes of flash -> 7E 05 <ok> <bytes>
A firmware's own DSP commands start at 0x7F11 (platform/dsp/gp5_dsp.h; 0x7F10 carries parameters):
`gp5_link.py raw <payload>` sends one and prints the reply. Nothing here writes the pedal's flash.
Needs `pip install mido python-rtmidi`.
"""
import struct, sys
import gp5tool
from gp5tool import MidiPorts, ask as _ask

STATUS = {0: "no answer", 1: "reply", 0x14: "ACK", 0x13: "NAK"}
UNITS = {0: "", 1: " %", 2: " dB", 3: " ms", 4: " Hz", 5: " st", 6: " cents", 7: ""}
FLAGS = [(1, "DSP"), (2, "preset"), (4, "saved"), (8, "read-only"), (16, "hidden")]


def params(link):
    """the firmware's parameter table (7E 12), with each value (7E 11): [dict]"""
    out = []
    while True:
        pl = ask(link, [0x7E, 0x12, len(out) & 0xFF, len(out) >> 8])
        if not pl or len(pl) < 15 or pl[2] != 1: break
        cnt, pid, fl, unit, mn, mx, df = struct.unpack_from("<HHBBhhh", pl, 3)
        name, group, choices = (bytes(pl[15:]).split(b"\0") + [b"", b"", b""])[:3]
        out.append(dict(id=pid, flags=fl, unit=unit, min=mn, max=mx, default=df, name=name.decode(), group=group.decode(),
                        choices=choices.decode().split("|") if fl & 0x80 else None))
        if len(out) >= cnt: break
    for k in range(0, len(out), 40):
        pl = ask(link, [0x7E, 0x11, k & 0xFF, k >> 8, min(40, len(out) - k)])
        for i in range(pl[2] if pl else 0):
            out[k + i]["value"] = struct.unpack_from("<h", pl, 3 + 2 * i)[0]
    return out


def text(link, p):
    v = p.get("value")
    if v is None: return "?"
    if p["choices"]: return p["choices"][v] if 0 <= v < len(p["choices"]) else str(v)
    if p["unit"] == 7:
        pl = ask(link, [0x7E, 0x15, p["id"] & 0xFF, p["id"] >> 8, v & 0xFF, (v >> 8) & 0xFF])
        if pl and len(pl) > 3 and pl[2] == 1: return bytes(pl[3:]).split(b"\0")[0].decode()
    if p["unit"] in (2, 6): return f"{v / 10:g}{UNITS[p['unit']]}"
    return f"{v}{UNITS.get(p['unit'], '')}"


def hx(b): return " ".join("%02x" % x for x in b)


def ask(link, payload, timeout=1.0):
    return _ask(link, bytes(payload), bytes(payload[:2]), timeout)


def open_link():
    ports = MidiPorts()
    names = [n for n in ports.names() if gp5tool.OPEN_PORT in n]
    if not names: sys.exit("no GP-5 running the open firmware found on USB")
    return ports.open(names[0])


def stats(link):
    pl = ask(link, [0x7E, 0x01])
    if pl is None: print("stats: no reply"); return
    fr, un, ucmd, err, bad, lost, acks, naks, off, nbytes = struct.unpack_from("<HHHBBBBBBH", pl, 2)
    print("frames %d  unsolicited %d (last cmd 0x%04x)  uart-errors %d  bad-frames %d  ring-lost %d  "
          "acks %d  naks %d  irq-off %d  bytes(lo16) %d" % (fr, un, ucmd, err, bad, lost, acks, naks, off, nbytes))
    if len(pl) > 16: print("last unsolicited payload:", hx(pl[16:]))


def raw(link, payload, wait_ms=300):
    """Stage `payload` (7E 03, 48 bytes a piece), then send it (7E 04). Returns (status, reply) or None."""
    for off in range(0, len(payload), 48):
        r = ask(link, [0x7E, 0x03, off & 0xFF, off >> 8] + list(payload[off:off + 48]))
        if r is None or len(r) < 3 or r[2] != 1: return None
    r = ask(link, [0x7E, 0x04, len(payload) & 0xFF, len(payload) >> 8, (wait_ms + 9) // 10], timeout=2.0)
    if r is None or len(r) < 4: return None
    return r[2], r[4:4 + r[3]]


def show_raw(link, payload):
    r = raw(link, payload)
    cmd = payload[0] | payload[1] << 8
    if r is None: print("cmd %04x (%d bytes): no reply from the firmware" % (cmd, len(payload))); return False
    st, rep = r
    print("cmd %04x (%d bytes): %s%s" % (cmd, len(payload), STATUS.get(st, hex(st)), "  " + hx(rep) if rep else ""))
    return st in (1, 0x14)


def flash_read(link, addr, length):
    out = bytearray()
    while len(out) < length:
        n = min(64, length - len(out))
        a = addr + len(out)
        pl = ask(link, [0x7E, 0x05, a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, a >> 24, n])
        if not pl or len(pl) < 3 or not pl[2]:
            sys.exit("no flash-read reply at 0x%x (needs an OpenGP5 platform with 7E 05)" % a)
        out += pl[3:3 + n]
    return bytes(out)


def main():
    a = sys.argv[1:] or ["stats"]
    if a[0] not in ("version", "stats", "raw", "load", "flash-read", "info", "params", "set", "presets", "preset", "log", "status"): sys.exit(__doc__)
    link = open_link()
    if a[0] == "version":
        pl = ask(link, [0x7E, 0x00])
        print("no reply" if pl is None else "DSP version 0x%08x" % struct.unpack_from("<I", pl, 3)[0]
              if len(pl) >= 7 and pl[2] else "DSP did not answer")
    elif a[0] == "stats":
        stats(link)
    elif a[0] == "load":
        r = raw(link, bytes.fromhex("047f0400"))
        if not r or r[0] != 1 or len(r[1]) < 16: print("no load report (needs platform DSP 1.0 or later)")
        else:
            last, worst, late = struct.unpack_from("<3I", r[1], 4)
            print("DSP load: last block %d cycles (%.1f %%), worst %d (%.1f %%), late blocks %d"
                  % (last, 100 * last / 278000, worst, 100 * worst / 278000, late))
    elif a[0] == "log":
        import time
        pos, follow = 0, "-f" in a[1:]
        try:
            while True:
                pl = ask(link, [0x7E, 0x18, *struct.pack("<I", pos)])
                if not pl or len(pl) < 12 or pl[2] != 1:
                    print("no log (a firmware built before OpenGP5 1.0)"); break
                at, end, n = struct.unpack_from("<IIB", pl, 3)
                if at > pos: print(f"... ({at - pos} bytes overwritten before they were read)")
                sys.stdout.write(bytes(pl[12:12 + n]).decode("ascii", "replace")); sys.stdout.flush()
                pos = at + n
                if pos >= end:
                    if not follow: break
                    time.sleep(0.3)
        except KeyboardInterrupt:
            pass
    elif a[0] == "status":
        pl = ask(link, [0x7E, 0x14] + ([1] if "--reset" in a[1:] else []))
        if not pl or len(pl) < 63 or pl[2] != 1: print("no status (a firmware built before OpenGP5 1.0)")
        else:
            v = struct.unpack_from("<15I", pl, 3)
            pct = lambda c, b: f"{100 * c / b:5.1f} %" if b else "?"
            print(f"control processor  audio link handler: {pct(v[1], v[0])} last, {pct(v[2], v[0])} worst of a block ({v[0]:,} cycles)")
            print(f"                   main loop longest pass: {v[3]:,} cycles ({100 * v[3] / v[0]:.0f} % of a block); RAM {v[4]:,} of {v[5]:,} bytes")
            if v[6]:
                print(f"DSP                {pct(v[8], v[7])} last, {pct(v[9], v[7])} worst of a block ({v[7]:,} cycles), {v[10]} late blocks")
                print(f"                   variables {v[11]:,} of {v[12]:,} bytes, GP5_LARGE {v[13]:,} of {v[14]:,} bytes")
            else: print("DSP                did not answer")
    elif a[0] == "info":
        pl = ask(link, [0x7E, 0x0D])
        if not pl or len(pl) < 7 or pl[2] != 1: print("no answer (an OpenGP5 firmware before 0.5.2, or Valeton's)")
        else:
            print(f"platform {pl[3]}.{pl[4]}.{pl[5]}, firmware id {bytes(pl[6:]).split(bytes(1))[0].decode()}")
            import json
            first = ask(link, [0x7E, 0x16, 0, 0])
            if first and len(first) >= 5 and first[2] == 1:
                total = first[3] | (first[4] << 8); blob = bytes(first[5:])
                while len(blob) < total:
                    nxt = ask(link, [0x7E, 0x16, len(blob) & 0xFF, len(blob) >> 8]); blob += bytes(nxt[5:])
                n = struct.unpack_from("<I", blob, 8)[0]
                m = json.loads(blob[12:12 + n].rstrip(bytes(1)))
                print(f"{m['name']} {m['version']}" + (f", by {m['author']}" if m.get("author") else "") + (f" ({m['url']})" if m.get("url") else ""))
            else: print("(no manifest: a firmware built before OpenGP5 1.0)")
    elif a[0] == "params":
        ps = params(link)
        if not ps: print("no parameters (a firmware without params.h, or before OpenGP5 1.0)")
        for p in ps:
            rng = "|".join(p["choices"]) if p["choices"] else f"{p['min']}..{p['max']}"
            print(f"{p['id']:3d}  {p['group'] + ' / ' if p['group'] else ''}{p['name']:<12s} {text(link, p):>12s}   ({rng}; "
                  + ", ".join(n for b, n in FLAGS if p["flags"] & b) + ")")
    elif a[0] == "set":
        if len(a) != 3: sys.exit(__doc__)
        ps = params(link)
        p = next((q for q in ps if q["name"].lower() == a[1].lower() or str(q["id"]) == a[1]), None)
        if not p: sys.exit(f"no parameter {a[1]} (there are: {', '.join(q['name'] for q in ps)})")
        v = p["choices"].index(next(c for c in p["choices"] if c.lower() == a[2].lower())) if p["choices"] and not a[2].lstrip("-").isdigit() else int(a[2], 0)
        pl = ask(link, [0x7E, 0x10, p["id"] & 0xFF, p["id"] >> 8, *struct.pack("<i", v)])
        p["value"] = params(link)[p["id"]]["value"]
        print(f"{p['name']} = {text(link, p)}" if pl and pl[2] == 1 else f"refused (read-only?)")
    elif a[0] == "presets":
        pl = ask(link, [0x7E, 0x17, 0])
        if not pl or pl[2] != 1: print("no presets"); return
        cnt, cur, dirty = pl[3], pl[4], pl[5]; used = struct.unpack_from("<Q", pl, 6)[0]
        for n in range(cnt):
            nm = ask(link, [0x7E, 0x17, 3, n]); name = bytes(nm[3:]).split(bytes(1))[0].decode() if nm else ""
            print(f"{n + 1:2d} {'*' if n == cur else ' '} {'(empty)' if not used >> n & 1 else name or '(no name)'}" + (" (changed)" if n == cur and dirty else ""))
    elif a[0] == "preset":
        if len(a) < 3 or a[1] not in ("load", "save", "name"): sys.exit(__doc__)
        n = int(a[2]) - 1
        if a[1] == "name":
            pl = ask(link, [0x7E, 0x17, 4, n, *" ".join(a[3:]).encode("ascii", "replace")[:12]])
        else:
            pl = ask(link, [0x7E, 0x17, 1 if a[1] == "load" else 2, n])
        print("done" if pl and pl[2] == 1 else "refused (an empty preset, or no such preset)")
    elif a[0] == "flash-read":
        if len(a) < 3: sys.exit(__doc__)
        addr, length = int(a[1], 0), int(a[2], 0)
        data = flash_read(link, addr, length)
        used = [i for i in range(0, len(data), 4096) if any(b != 0xFF for b in data[i:i + 4096])]
        print("0x%06x..0x%06x: %s" % (addr, addr + length - 1, "erased (all FF)" if not used else
              "%d of %d 4 KB sectors hold data, first at 0x%06x" % (len(used), (length + 4095) // 4096, addr + used[0])))
        if len(a) > 3: open(a[3], "wb").write(data); print("saved", a[3])
    else:
        show_raw(link, bytes.fromhex("".join(a[1:])))
    link.close()


if __name__ == "__main__":
    main()
