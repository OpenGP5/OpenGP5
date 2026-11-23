"""Offline tests for gp5tool.py against a simulated pedal; never opens a MIDI port."""
import struct, unittest
from pathlib import Path

import gp5tool
from gp5tool import Problem, MODE_OPEN, MODE_STOCK, MODE_LOADER, MODE_SILENT
from gp5_proto import build_sysex, parse_sysex, CLASS_CMD, CLASS_DATA, CHUNK

ROOT = Path(__file__).resolve().parents[1]
STOCK = ROOT / "firmware" / "GP-5_USB_V1.0.6_20250626.bin"
ACK, NAK = bytes([0x14, 0x08, 0x00]), bytes([0x14, 0x08, 0x01])


class FakePedal:
    """Ports + loader protocol as observed on the test unit."""

    def __init__(self, mode=MODE_STOCK, nak_every=0, stall_begin=False, die_at=None, stay_in_loader=False, mute=False,
                 after=None, silent_queries=0):
        self.mode, self.nak_every, self.stall_begin, self.die_at = mode, nak_every, stall_begin, die_at
        self.stay_in_loader, self.mute, self.after = stay_in_loader, mute, after
        self.silent_queries = silent_queries
        self.present = True
        self.written = {}          # type -> bytes, committed at END_SEC
        self.ended = False
        self.sent_cmds = []
        self.cur = None
        self.n = 0

    # --- ports
    def names(self):
        if not self.present: return []
        return ["GP-5 Open FW:GP-5 Open FW MIDI 1 16:0"] if self.mode == MODE_OPEN else ["GP-5:GP-5 MIDI 1 16:0"]

    def open(self, name):
        assert name in self.names()
        return FakeLink(self)

    # --- protocol
    def handle(self, raw):
        cls, idx, ln, pl, ok = parse_sysex(raw)
        pl = bytes(pl)
        if not ok or self.mute: return []
        if cls == CLASS_CMD: self.sent_cmds.append(pl[:2])
        if self.mode != MODE_LOADER:
            if cls == CLASS_CMD and pl == b"\x12\x30":
                return [] if self.mode in (MODE_OPEN, MODE_SILENT) else [bytes([0x11, 0x30, 0x01, 0x00])]
            if cls == CLASS_CMD and pl == b"\x11\x6f":
                self.mode = MODE_LOADER
                return [ACK]
            return []
        if cls == CLASS_CMD:
            if pl == b"\x12\x30":
                if self.silent_queries:
                    self.silent_queries -= 1
                    return []
                return [bytes([0x11, 0x30, 0x01, 0x01])]
            if pl == b"\x11\x60": return [ACK]
            if pl[:2] == b"\x11\x61":
                if self.stall_begin: return []
                count, length = struct.unpack_from("<II", pl, 6)
                self.cur = dict(type=pl[2], count=count, length=length, buf=bytearray(), next=0)
                return [ACK]
            if pl[:2] == b"\x11\x6e" and pl[2] == 0x6e:
                self.ended = True
                if not self.stay_in_loader: self.mode = self.after or (MODE_STOCK if 0x62 not in self.written else MODE_OPEN)
                return [ACK]
            if pl[:2] == b"\x11\x6e":
                c = self.cur
                assert c and pl[2] == c["type"] and c["next"] == c["count"] and len(c["buf"]) == c["length"]
                assert c["buf"][:2] == bytes([0x11, c["type"]])
                self.written[c["type"]] = bytes(c["buf"][2:])
                self.cur = None
                return [ACK]
            return []
        if cls == CLASS_DATA and self.cur:
            self.n += 1
            if self.die_at and self.n >= self.die_at: return []
            if self.nak_every and self.n % self.nak_every == 0: return [NAK]
            c = self.cur
            if idx != c["next"] & 0xFF: return [NAK]
            c["buf"] += pl; c["next"] += 1
            return [ACK]
        return []


class FakeLink:
    def __init__(self, pedal): self.p, self.q = pedal, []
    def send(self, raw): self.q += [build_sysex(CLASS_CMD, 0, r) for r in self.p.handle(raw)]
    def recv(self, timeout): return self.q.pop(0) if self.q else None
    def flush(self): self.q.clear()
    def close(self): pass


def fast():
    T = gp5tool.Timing
    T.reply = T.ack = T.begin = T.final = 0.01
    T.begin_settle = T.settle = T.retry_delay = T.loader_retry_delay = 0.0
    T.reenum, T.poll = 0.05, 0.001


NEED_STOCK = unittest.skipUnless(STOCK.is_file(), "needs Valeton's stock image in firmware/ (not in the public repo)")


def tiny_app(n=0x200):
    """Smallest section the checks accept as an app: 41 `j` vectors into the image, magic at 0xC0."""
    a = bytearray(b"\xff" * n)
    for o in range(0, 0xA4, 4): a[o:o + 4] = ((0x48 << 24) | ((0x108 - o) >> 1)).to_bytes(4, "big")
    a[0xC0:0xC4] = bytes.fromhex("c9bdbeb0")
    return bytes(a)


def tiny_dsp():
    """Smallest DSP section the checks accept: header (vector table at 0x4400), then {stack, reset}."""
    d = bytearray(b"\xff" * 8 + b"V106" + struct.pack("<I", 0x4400) + bytes(0x400 - 16))
    d += struct.pack("<2I", 0x20008000, 0x4409) + bytes(0x100)
    return bytes(d)


class PartialImages(unittest.TestCase):
    """This project's images: only the sections we build, no Valeton file needed."""
    def test_pack_then_load(self):
        out = gp5tool.make_image({0x62: tiny_app(), 0x63: tiny_dsp()})
        sections, lines = gp5tool.inspect(out)
        self.assertEqual([t for t, _ in sections], [0x63, 0x62])       # always the pedal's order
        self.assertEqual(dict(sections)[0x62], tiny_app())
        self.assertEqual(len(gp5tool.pick(sections, ["all"])), 2)
        self.assertEqual(len(gp5tool.make_image({0x62: tiny_app()})), 0x38 + 16 + 0x200)

    def test_bad_partial_images_are_refused(self):
        good = bytearray(gp5tool.make_image({0x62: tiny_app(), 0x63: tiny_dsp()}))
        swapped = bytearray(good); swapped[0x38 + 3], swapped[0x48 + 3] = 0x62, 0x63     # order swapped
        wrong_addr = bytearray(good); wrong_addr[0x48 + 4] = 1                         # app not at 0
        for bad in (swapped, wrong_addr, good[:-1]):
            with self.assertRaises(Problem): gp5tool.inspect(bytes(bad))
        with self.assertRaises(Problem): gp5tool.make_image({0x62: bytes(4096)})       # not bootable
        with self.assertRaises(Problem): gp5tool.make_image({})

    def test_combine_on_a_partial_base(self):
        base = gp5tool.make_image({0x62: tiny_app(), 0x63: tiny_dsp()})
        out = gp5tool.combine(base, {0x62: tiny_app(0x300)})
        self.assertEqual(dict(gp5tool.inspect(out)[0])[0x62], tiny_app(0x300))


@NEED_STOCK
class ImageChecks(unittest.TestCase):
    def setUp(self): self.blob = STOCK.read_bytes()

    def test_stock_image_is_valid_and_recognised(self):
        sections, lines = gp5tool.inspect(self.blob)
        self.assertEqual([t for t, _ in sections], [0x63, 0x67, 0x66, 0x62, 0x65])
        self.assertTrue(all("stock V1.0.6" in l for l in lines), lines)

    def test_corrupt_byte_is_refused(self):
        b = bytearray(self.blob); b[len(b) // 2] ^= 1
        with self.assertRaises(Problem): gp5tool.inspect(bytes(b))

    def test_truncated_and_foreign_files_are_refused(self):
        for bad in (self.blob[:-1], b"hello" * 100, b""):
            with self.assertRaises(Problem): gp5tool.inspect(bad)

    def test_app_and_dsp_boot_checks(self):
        sec = dict(gp5tool.inspect(self.blob)[0])
        self.assertEqual(gp5tool.check_app(sec[0x62]), [])
        self.assertEqual(gp5tool.check_dsp(sec[0x63]), [])
        a = bytearray(sec[0x62]); a[0xC0] ^= 0xFF
        self.assertTrue(gp5tool.check_app(bytes(a)))
        a = bytearray(sec[0x62]); a[8] = 0
        self.assertTrue(gp5tool.check_app(bytes(a)))
        d = bytearray(sec[0x63]); vt = struct.unpack_from("<I", d, 0x0C)[0] - 0x4000
        struct.pack_into("<I", d, vt + 4, 0x4500)            # reset vector without the Thumb bit
        self.assertTrue(gp5tool.check_dsp(bytes(d)))
        d = bytearray(sec[0x63]); struct.pack_into("<I", d, vt, 0x08000000)
        self.assertTrue(gp5tool.check_dsp(bytes(d)))

    def test_section_choice(self):
        sections, _ = gp5tool.inspect(self.blob)
        self.assertEqual([t for t, _ in gp5tool.pick(sections, ["app"])], [0x62])
        self.assertEqual([t for t, _ in gp5tool.pick(sections, ["app,dsp"])], [0x63, 0x62])      # image order
        self.assertEqual(len(gp5tool.pick(sections, ["all"])), 5)
        for bad in ([], ["bootloader"]):
            with self.assertRaises(Problem): gp5tool.pick(sections, bad)


class PortNames(unittest.TestCase):
    def test_input_and_output_are_paired_on_every_system(self):
        class Mido:                                    # names as seen on macOS, Linux and Windows, 2026-11-10
            def __init__(self, outs, ins): self.o, self.i = outs, ins
            def get_output_names(self): return self.o
            def get_input_names(self): return self.i
            def open_output(self, n): return ("out", n)
            def open_input(self, n): return ("in", n)
        cases = [
            (["Midi Through:Midi Through Port-0 14:0", "GP-5 Open FW:GP-5 Open FW MIDI 1 16:0"],) * 2 + ("GP-5 Open FW:GP-5 Open FW MIDI 1 16:0",) * 2,
            (["GP-5:GP-5 MIDI 1 16:0"], ["GP-5:GP-5 MIDI 1 16:0"], "GP-5:GP-5 MIDI 1 16:0", "GP-5:GP-5 MIDI 1 16:0"),
            (["GP-5 Open FW"], ["GP-5 Open FW"], "GP-5 Open FW", "GP-5 Open FW"),
            (["Microsoft GS Wavetable Synth 0", "Focusrite USB MIDI 1", "GP-5 Open FW 2"], ["Focusrite USB MIDI 0", "GP-5 Open FW 1"],
             "GP-5 Open FW 2", "GP-5 Open FW 1"),
            (["Microsoft GS Wavetable Synth 0", "GP-5 2"], ["GP-5 1"], "GP-5 2", "GP-5 1"),
        ]
        for outs, ins, want_out, want_in in cases:
            ports = gp5tool.MidiPorts.__new__(gp5tool.MidiPorts); ports.mido = Mido(outs, ins)
            mine = [n for n in ports.names() if "GP-5" in n]
            self.assertEqual(mine, [want_out])
            link = ports.open(want_out)
            self.assertEqual((link.out, link.inp), (("out", want_out), ("in", want_in)))
        self.assertEqual(gp5tool.port_base("GP-5"), "GP-5")


@NEED_STOCK
class Combine(unittest.TestCase):
    def test_takes_sections_and_keeps_the_rest(self):
        base = STOCK.read_bytes()
        secs = dict(gp5tool.inspect(base)[0])
        app = secs[0x62][:60000]                           # still a valid app: vectors and magic are at the start
        dsp = bytearray(secs[0x63]); dsp[0x3000] ^= 0x55   # a different, still bootable DSP section
        out = gp5tool.combine(base, {0x62: app, 0x63: bytes(dsp)})
        got = dict(gp5tool.inspect(out)[0])
        self.assertEqual(got[0x62], app); self.assertEqual(got[0x63], bytes(dsp))
        for t in (0x67, 0x66, 0x65): self.assertEqual(got[t], secs[t])
        self.assertEqual(gp5tool.combine(base, {}), base)  # nothing taken = the same image, byte for byte

    def test_unbootable_result_is_refused(self):
        base = STOCK.read_bytes()
        with self.assertRaises(Problem): gp5tool.combine(base, {0x62: bytes(4096)})


class FlashRuns(unittest.TestCase):
    def setUp(self):
        fast()
        if STOCK.is_file():
            self.sections, _ = gp5tool.inspect(STOCK.read_bytes())
            self.small = [(0x63, dict(self.sections)[0x63]), (0x65, dict(self.sections)[0x65][:5000])]
        else:                                    # public checkout: the same runs with this project's sections
            dsp = tiny_dsp() + b"\xff" * (0xC348 - len(tiny_dsp()))      # stock DSP section length
            self.sections = [(0x63, dsp), (0x62, tiny_app(0x10000))]
            self.small = [(0x63, dsp), (0x65, bytes(5000))]
        self.log = []

    def run_flash(self, pedal, sections=None):
        return gp5tool.flash(pedal, sections or self.small, self.log.append)

    def test_from_stock_firmware(self):
        p = FakePedal(MODE_STOCK)
        mode = self.run_flash(p)
        self.assertEqual(mode, MODE_STOCK)
        self.assertEqual(p.written, dict(self.small))
        self.assertTrue(p.ended)
        self.assertIn(b"\x11\x6f", p.sent_cmds)

    def test_from_open_firmware_and_app_section(self):
        p = FakePedal(MODE_OPEN)
        app = [(0x62, dict(self.sections)[0x62][:40000])]
        self.assertEqual(self.run_flash(p, app), MODE_OPEN)
        self.assertEqual(p.written, dict(app))

    def test_already_in_update_mode_sends_no_restart(self):
        p = FakePedal(MODE_LOADER)
        self.run_flash(p)
        self.assertNotIn(b"\x11\x6f", p.sent_cmds)
        self.assertEqual(p.written, dict(self.small))

    def test_naks_are_retried_and_data_is_exact(self):
        p = FakePedal(MODE_STOCK, nak_every=97)
        self.run_flash(p)
        self.assertEqual(p.written, dict(self.small))
        self.assertTrue(any("repeated" in l for l in self.log))

    def test_stall_at_erase_reports_nothing_written(self):
        p = FakePedal(MODE_LOADER, stall_begin=True)
        with self.assertRaises(Problem) as c: self.run_flash(p)
        self.assertIn("Nothing was written", str(c.exception))
        self.assertEqual(p.written, {}); self.assertFalse(p.ended)

    def test_dying_mid_section_reports_incomplete_and_never_ends_image(self):
        p = FakePedal(MODE_LOADER, die_at=500)
        with self.assertRaises(Problem) as c: self.run_flash(p)
        self.assertIn("INCOMPLETE", str(c.exception)); self.assertIn("Recovery", str(c.exception))
        self.assertFalse(p.ended)

    def test_no_pedal_and_silent_pedal(self):
        p = FakePedal(); p.present = False
        with self.assertRaises(Problem): self.run_flash(p)
        p = FakePedal(MODE_STOCK, mute=True)
        with self.assertRaises(Problem) as c: self.run_flash(p)
        self.assertEqual(p.written, {})
        self.assertIn("does not answer", str(c.exception))

    def test_app_that_answers_nothing_but_the_restart_command(self):
        # hardware 2026-11-10: Valeton's app with our DSP firmware ignores 12 30 but obeys 11 6F
        p = FakePedal(MODE_SILENT)
        self.assertEqual(gp5tool.find(p), ("GP-5:GP-5 MIDI 1 16:0", MODE_SILENT))
        app = [(0x62, dict(self.sections)[0x62][:20000])]
        self.assertEqual(self.run_flash(p, app), MODE_OPEN)
        self.assertEqual(p.written, dict(app))

    def test_port_that_errors_while_the_pedal_restarts_is_not_fatal(self):
        # Windows, 2026-11-10: the old port is still listed after END_IMAGE and send() raises.
        p = FakePedal(MODE_STOCK)                       # ends on stock firmware: detection has to open the port
        real_open, state = p.open, {"n": 0}
        class Broken:
            def flush(self): pass
            def send(self, raw): raise RuntimeError("MidiOutWinMM::sendMessage: error sending sysex message.")
            def recv(self, t): return None
            def close(self): raise RuntimeError("already gone")
        def flaky_open(name):
            if p.ended:
                state["n"] += 1
                if state["n"] <= 3: return Broken()
                if state["n"] == 4: raise OSError("port vanished")
            return real_open(name)
        p.open = flaky_open
        gp5tool.Timing.reenum = 2.0
        self.assertEqual(self.run_flash(p, [self.small[0]]), MODE_STOCK)
        self.assertGreaterEqual(state["n"], 5)

    def test_loader_state_query_is_retried(self):
        p = FakePedal(MODE_LOADER, silent_queries=2)
        link = gp5tool.open_loader(p, p.names()[0])
        self.assertIsNotNone(link)
        self.assertEqual(p.silent_queries, 0)
        link.close()

    def test_port_listing_error_during_restart_is_not_fatal(self):
        p = FakePedal(MODE_STOCK)                       # ends on stock firmware: detection has to open the port
        real_names, state = p.names, {"n": 0}
        def flaky_names():
            if p.ended:
                state["n"] += 1
                if state["n"] <= 3:
                    raise RuntimeError("MidiInCore::getPortName: the 'portNumber' argument (1) is invalid.")
            return real_names()
        p.names = flaky_names
        gp5tool.Timing.reenum = 2.0
        self.assertEqual(self.run_flash(p, [self.small[0]]), MODE_STOCK)
        self.assertGreaterEqual(state["n"], 4)

    def test_ending_in_the_silent_state_is_reported_as_such(self):
        p = FakePedal(MODE_STOCK, after=MODE_SILENT)
        self.assertEqual(self.run_flash(p, [self.small[0]]), MODE_SILENT)

    def test_pedal_that_stays_in_update_mode_is_reported(self):
        p = FakePedal(MODE_LOADER, stay_in_loader=True)
        self.assertEqual(self.run_flash(p), MODE_LOADER)
        self.assertTrue(p.ended)

    def test_wire_frames_match_the_proven_flasher(self):
        from gp5_proto import build_update_stream
        sent = []
        p = FakePedal(MODE_LOADER)
        orig = p.handle
        p.handle = lambda raw: (sent.append(bytes(raw)), orig(raw))[1]
        data = dict(self.sections)[0x63]
        self.run_flash(p, [(0x63, data)])
        want = [m for _, m in build_update_stream(data, 0x63)]
        got = [s for s in sent if parse_sysex(s)[3][:2] != b"\x12\x30"]
        self.assertEqual(got, want)



class TickFix(unittest.TestCase):
    """gp5tool.py tickfix: status and --apply through the DSP's patcher (link commands 0x7F01 / 0x7F02)."""

    def run_with(self, state, apply=False, reply_ok=True):
        import argparse, io, contextlib, gp5_link
        sent = []
        class L:
            def close(self): pass
        def raw(link, payload, wait_ms=300):
            sent.append(bytes(payload))
            if not reply_ok: return None
            return 1, bytes(payload[:2]) + bytes([36, 0]) + struct.pack("<I", state) + bytes(32)
        old = gp5_link.open_link, gp5_link.raw
        gp5_link.open_link, gp5_link.raw = (lambda: L()), raw
        try:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = gp5tool.cmd_tickfix(argparse.Namespace(apply=apply))
            return rc, out.getvalue(), sent
        finally:
            gp5_link.open_link, gp5_link.raw = old

    def test_status_reads_only(self):
        rc, out, sent = self.run_with(0)
        self.assertEqual(rc, 0); self.assertIn("not applied", out)
        self.assertEqual(sent, [bytes([0x01, 0x7F, 4, 0])])

    def test_apply_sends_the_magic(self):
        rc, out, sent = self.run_with(1, apply=True)
        self.assertEqual(rc, 0); self.assertIn("applied", out)
        self.assertEqual(sent, [bytes([0x02, 0x7F, 12, 0]) + b"TICKFIX!"])

    def test_unknown_updater_is_a_failure(self):
        rc, out, _ = self.run_with(0x10, apply=True)
        self.assertEqual(rc, 1); self.assertIn("isn't the known one", out)

    def test_no_patcher(self):
        with self.assertRaises(Problem): self.run_with(0, reply_ok=False)


class MidiAbortRetry(unittest.TestCase):
    """macOS 'RtMidi -304' ends the program from C++; gp5tool.guarded() runs the command again only if nothing was sent
    yet. The aborts are simulated (test switches in gp5tool.py), on any system (GP5_MIDI_GUARD)."""

    def run_tool(self, script, args, **env):
        import os, subprocess, sys
        e = dict(os.environ, GP5_MIDI_GUARD="1", GP5_MIDI_RETRY_WAIT="0.05", **env)
        e.pop("GP5_MIDI_CHILD", None)
        r = subprocess.run([sys.executable, str(Path(__file__).parent / script), *args], env=e, capture_output=True, text=True)
        return r.returncode, r.stderr

    def test_an_abort_before_sending_is_retried(self):
        rc, err = self.run_tool("gp5tool.py", ["check", str(Path(__file__).parent / "nonexistent.bin")], GP5_TEST_MIDI_ABORT="2")
        self.assertNotIn("Trying again", err)                 # (check opens no MIDI: never aborts)
        rc, err = self.run_tool("gp5tool.py", ["status"], GP5_TEST_MIDI_ABORT="2")
        self.assertEqual(err.count("Trying again"), 2)
        self.assertNotEqual(rc, 134)                          # the third attempt ran (pedal or not, mido or not)

    def test_it_gives_up_after_three(self):
        rc, err = self.run_tool("gp5_link.py", ["info"], GP5_TEST_MIDI_ABORT="9")
        self.assertEqual(rc, 134)
        self.assertIn("3 times; nothing was sent", err)

    def test_an_abort_after_sending_is_not_retried(self):
        rc, err = self.run_tool("gp5tool.py", ["status"], GP5_TEST_MIDI_ABORT_AFTER_SEND="1")
        self.assertEqual(rc, 134)
        self.assertNotIn("Trying again", err)
        self.assertIn("already talked to the pedal", err)


if __name__ == "__main__":
    unittest.main()
