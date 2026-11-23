#!/usr/bin/env python3
"""j7_nudge.py against a simulated stalled DSP (no OpenOCD, no pedal): it refuses anything but the known stall, and
on the stall makes exactly the RAM changes it should. The vector table here is made up; its fingerprint is swapped in."""
import hashlib, struct, sys, os, unittest
sys.path.insert(0, os.path.dirname(__file__))
import j7_nudge as j


class FakeDSP:
    def __init__(self):
        self.mem = {}
        self.regs = {"pc": 0x2000087C, "msp": j.STALL_MSP, "lr": j.STALL_LR, "r0": j.STALL_R0, "r1": 0}
        self.table = [0x20003260] + [0x20000013 + 2 * i for i in range(1, 128)]
        for i, w in enumerate(self.table): self.mem[4 * i] = w
        self.mem[j.STALL_MSP] = 12; self.mem[j.VTOR] = 0; self.mem[j.TICK_PTR] = j.TICK
        self.mem[j.TICK] = 0; self.mem[j.WRONG] = 0x12F; self.mem[j.DHCSR] = 0
        for i, h in enumerate((0x480A, 0x6801, 0x1C49, 0x6001, 0x4770)): self.mem[("h", j.HELPER + 2 * i)] = h
        self.halted = False; self.log = []

    def cmd(self, text):
        self.log.append(text)
        if text.startswith("halt"): self.halted = True
        if text == "resume":
            self.halted = False
            if self.mem[j.VTOR] == j.TABLE_COPY and self.mem.get(j.TABLE_COPY + 4 * j.SYSTICK) == j.HELPER | 1:
                self.mem[j.TICK] += 40                     # the timer counts in the right place now
        return ""

    def read32(self, a, n=1): return [self.mem.get(a + 4 * i, 0) for i in range(n)]
    def read16(self, a, n=1): return [self.mem.get(("h", a + 2 * i), 0) for i in range(n)]
    def write32(self, a, ws):
        for i, w in enumerate(ws): self.mem[a + 4 * i] = w
    def reg(self, n): return self.regs[n]


class Test(unittest.TestCase):
    def setUp(self):
        self.dsp = FakeDSP()
        self.saved = j.TABLE_SHA
        j.TABLE_SHA = hashlib.sha256(struct.pack("<64I", *self.dsp.table[:64])).hexdigest()

    def tearDown(self): j.TABLE_SHA = self.saved

    def test_nudge_on_the_known_stall(self):
        self.assertEqual(j.cmd_nudge(self.dsp), 0)
        m = self.dsp.mem
        self.assertEqual(m[j.VTOR], j.TABLE_COPY)
        copy = [m[j.TABLE_COPY + 4 * i] for i in range(128)]
        want = list(self.dsp.table); want[j.SYSTICK] = j.HELPER | 1
        self.assertEqual(copy, want)
        self.assertEqual(m[j.WRONG], 2)
        self.assertGreater(m[j.TICK], 0)
        self.assertFalse(self.dsp.halted)

    def refused(self, change):
        change(self.dsp)
        before = dict(self.dsp.mem)
        with self.assertRaises(j.Problem): j.cmd_nudge(self.dsp)
        self.assertEqual(self.dsp.mem, before)          # nothing written
        self.assertFalse(self.dsp.halted)              # running again

    def test_refuses_a_running_pedal(self): self.refused(lambda d: d.regs.update(pc=0x00001234))
    def test_refuses_when_the_timer_counts(self): self.refused(lambda d: d.regs.update(r1=5))
    def test_refuses_a_second_nudge(self): self.refused(lambda d: d.mem.update({j.VTOR: j.TABLE_COPY}))
    def test_refuses_another_table(self): self.refused(lambda d: d.mem.update({4: 0xDB9}))
    def test_refuses_another_helper(self): self.refused(lambda d: d.mem.update({("h", j.HELPER): 0}))

    def test_refuses_when_already_halted(self):
        self.dsp.mem[j.DHCSR] = 0x20000
        with self.assertRaises(j.Problem): j.cmd_nudge(self.dsp)
        self.assertEqual(self.dsp.mem[j.VTOR], 0)

    def test_check_reads_only(self):
        before = dict(self.dsp.mem)
        self.assertEqual(j.cmd_check(self.dsp), 0)
        self.assertEqual(self.dsp.mem, before)
        self.dsp.regs["pc"] = 0x1000
        self.assertEqual(j.cmd_check(self.dsp), 1)

    def test_no_valeton_bytes(self):
        src = open(j.__file__).read()
        self.assertNotIn("0xdb9", src.lower())         # the table is read from the pedal, never written here


if __name__ == "__main__":
    unittest.main()
