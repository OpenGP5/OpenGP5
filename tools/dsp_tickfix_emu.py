#!/usr/bin/env python3
"""
Offline check of the resident tick-literal patch (platform/dsp/tickpatch.h, docs/platform/dsp-updater-fault.md).

  dsp_tickfix_emu.py <OpenGP5 image (its DSP part carries the patcher)> <resident dump>

The resident dump is DSP flash 0x0..0x3FFF read from a pedal over J7 (it is not in Valeton's update file).

Part A - the patcher. DSP flash = resident dump (0x0..0x3FFF) + the image's section 0x63 at 0x4000.
Our firmware runs in Unicorn (Cortex-M4) and gets the link commands 0x7F01 (status) and 0x7F02
(apply). The flash controller (SFI, 0x40050000) is a coarse model: ready bits always set, a page
program (command 0x32) ANDs the data bytes into the flash model. Checked: the only flash bytes that
change are 0xb60-0xb61 and 0x16dc-0x16dd (c0 0d -> 00 00); wrong magic, an unknown resident, and a
second run write nothing; a half-done patch is finished; and, in a "strict" variant where mapped
flash is only readable with the SFI+0x30 value from before the call, the RAM stub puts it back.

Part B - the stall. The resident runs from reset with the update pin low (0x40011000 bit 19 = 0).
SysTick and the UART interrupts are delivered by hand. From the flash reset command (0x99) until
flash init rewrites SFI+0x30, every flash word has bit 7 of byte 0 cleared (the read fault measured
on the test unit, taken at its worst). Checked: the unpatched resident hangs in the 12-tick wait
with the handler counting at 0x20000d40, and a J7-style nudge frees it (= what the bench shows); the
patched resident gets through with no help and answers a BEGIN frame with the status ACK.

No hardware is touched. Needs `unicorn`.
"""
import struct, subprocess, sys, tempfile
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[1]
UART, ADMA, SFI, ST = 0x40005000, 0x40030000, 0x40050000, 0x20007F00
LIT_A, LIT_B = 0xB60, 0x16DC
FLASH_LEN = 0x20000
ACK = b"\x01\x04\x00\x14"
REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5,
        UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR]


def crc8(d):
    c = 0
    for b in d:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def frame(pl):
    n = len(pl) + 4
    return bytes([crc8(pl), n & 0xFF, n >> 8, 1]) + pl


def new_mu():
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for base, size in ((0, 0x200000), (0x10000000, 0x20000), (0x1FFFF000, 0x1000), (0x20000000, 0x10000),
                       (0x40000000, 0x100000), (0x50000000, 0x1000), (0xE0000000, 0x100000)):
        mu.mem_map(base, size)
    return mu


class Sfi:
    """Flash controller + chip, as far as the resident's driver uses it."""

    def __init__(self, mu, flash, mode0, strict=False, need10=False):
        self.mu, self.flash, self.strict, self.good = mu, bytearray(flash), strict, mode0
        self.need10 = need10   # commands only run while SFI+0x10 == 0 (as in the resident's update mode)
        self.refused = 0
        self.cmd = 0
        self.prog_addr = self.prog_left = 0
        self.log = []          # (command byte, address, length)
        self.written = []      # (address, byte) programmed
        self.window = False    # read fault active
        self.window_seen = 0
        self.readable = True
        mu.mem_write(SFI + 0x30, struct.pack("<I", mode0))
        mu.mem_write(SFI + 0x10, struct.pack("<I", 0x81))      # normal-mode value read over J7, 2026-11-10
        self.sync()
        mu.hook_add(UC_HOOK_MEM_READ, self.rd, begin=SFI, end=SFI + 0xFFF)
        mu.hook_add(UC_HOOK_MEM_WRITE, self.wr, begin=SFI, end=SFI + 0xFFF)

    def sync(self):
        if not self.readable:
            img = b"\xde" * FLASH_LEN
        elif self.window:
            img = bytearray(self.flash)
            img[0::4] = bytes(b & 0x7F for b in img[0::4])
            img = bytes(img)
        else:
            img = bytes(self.flash)
        self.mu.mem_write(0, img)

    def rd(self, mu, acc, addr, size, val, _):
        o = addr - SFI
        if o == 0x24: mu.mem_write(addr, struct.pack("<I", 1))
        elif o == 0x18: mu.mem_write(addr, struct.pack("<I", 3))
        elif o == 0x100:
            c = self.cmd >> 24
            mu.mem_write(addr, struct.pack("<I", {0x05: 0x00, 0x35: 0x02}.get(c, 0)))  # SR1 idle, SR2 QE set

    def wr(self, mu, acc, addr, size, val, _):
        o = addr - SFI
        if o == 0x0C:
            if self.need10 and struct.unpack("<I", mu.mem_read(SFI + 0x10, 4))[0] != 0:
                self.refused += 1
                return
            self.cmd = val
            c = val >> 24
            a = struct.unpack("<I", mu.mem_read(SFI, 4))[0]
            n = struct.unpack("<I", mu.mem_read(SFI + 8, 4))[0]
            self.log.append((c, a, n))
            if c == 0x32: self.prog_addr, self.prog_left = a & 0xFFFFFF, n
            elif c == 0x20:
                a &= 0xFFF000
                self.flash[a:a + 0x1000] = b"\xff" * 0x1000
                self.sync()
            elif c == 0x99:
                self.window = True; self.window_seen += 1
                self.sync()
        elif o == 0x100:
            for k in range(size):
                if self.prog_left:
                    b = (val >> (8 * k)) & 0xFF
                    self.flash[self.prog_addr] &= b
                    self.written.append((self.prog_addr, b))
                    self.prog_addr += 1; self.prog_left -= 1
            if not self.prog_left: self.sync()
        elif o == 0x30:
            ok = (val == self.good) if self.strict else True
            if self.window and not (val & 0x60000000):
                self.window = False        # flash init has set the read mode up again
            if ok != self.readable or True:
                self.readable = ok
                self.sync()


def part_a(sec, resident, title, strict=False, mutate=None, expect_change=True, need10=False):
    flash = bytearray(resident[:FLASH_LEN].ljust(FLASH_LEN, b"\xff"))
    flash[0x4000:0x4000 + len(sec)] = sec
    if mutate: mutate(flash)
    before = bytes(flash)
    mu = new_mu()
    sfi = Sfi(mu, flash, 0x12345678 if strict else 0x20003000, strict, need10)
    rx, tx = bytearray(), bytearray()

    def rd(mu, acc, addr, size, val, _):
        if addr == UART + 0x0C: mu.mem_write(addr, struct.pack("<I", min(len(rx), 31) << 4))
        elif addr == UART + 0x00: mu.mem_write(addr, struct.pack("<I", rx.pop(0) if rx else 0))
        elif addr == ADMA + 0x00: mu.mem_write(addr, struct.pack("<I", 1))
        elif addr == ADMA + 0x04: mu.mem_write(addr, struct.pack("<I", 0xF))

    def wr(mu, acc, addr, size, val, _):
        if addr == UART + 0x00: tx.append(val & 0xFF)

    mu.hook_add(UC_HOOK_MEM_READ, rd, begin=0x40000000, end=0x4004FFFF)
    mu.hook_add(UC_HOOK_MEM_WRITE, wr, begin=0x40000000, end=0x4004FFFF)
    sp, reset = struct.unpack_from("<2I", sec, 0x400)
    mu.reg_write(UC_ARM_REG_SP, sp)
    pc = [reset]

    def run(n):
        mu.emu_start(pc[0] | 1, 0, count=n)
        pc[0] = mu.reg_read(UC_ARM_REG_PC)

    def cmd(pl):
        tx.clear(); n0 = len(sfi.log)
        rx.extend(frame(pl)); run(900000)
        out = bytes(tx)
        # drop the periodic 0x4001 reports, keep our reply
        rep = None
        i = 0
        while i + 4 <= len(out):
            if out[i:i + 4] == ACK: i += 4; continue
            ln = out[i + 1] | out[i + 2] << 8
            body = out[i + 4:i + ln]
            if body[:2] == pl[:2]: rep = struct.unpack("<8I", body[4:36])
            i += ln
        return rep, sfi.log[n0:]

    run(400000)
    print(f"-- {title}")
    ok = True
    names = "state lit_b60 lit_16dc hash sfi30 resA resB stage".split()

    def show(what, rep, log):
        print(f"   {what:22s} " + (" ".join(f"{k}={v:#x}" for k, v in zip(names, rep)) if rep else "NO REPLY")
              + f"   flash commands: {[f'{c:02x}@{a:x}+{n}' for c, a, n in log] or 'none'}")

    r, log = cmd(struct.pack("<HH", 0x7F01, 4)); show("status", r, log)
    ok &= r is not None and not log
    s0 = r[0] if r else None
    r, log = cmd(struct.pack("<HH", 0x7F02, 12) + b"TICKFIX?"); show("apply, wrong magic", r, log)
    ok &= r is not None and r[0] == 0x30 and not log
    r, log = cmd(struct.pack("<HH", 0x7F02, 12) + b"TICKFIX!"); show("apply", r, log)
    ok &= r is not None
    s1 = r[0] if r else None
    r, log = cmd(struct.pack("<HH", 0x7F02, 12) + b"TICKFIX!"); show("apply again", r, log)
    ok &= r is not None and not log
    diff = {i: (before[i], sfi.flash[i]) for i in range(FLASH_LEN) if before[i] != sfi.flash[i]}
    live = bytes(mu.mem_read(0, FLASH_LEN)) == bytes(sfi.flash)
    print(f"   flash bytes changed: { {hex(k): f'{a:02x}->{b:02x}' for k, (a, b) in diff.items()} or 'none'}; "
          f"mapped flash readable afterwards: {live}; fault {st(mu, 3):#x}; states {s0:#x} -> {s1:#x}")
    ok &= live and st(mu, 3) == 0
    if expect_change:
        want = {a: 0 for a in (LIT_A, LIT_A + 1, LIT_B, LIT_B + 1) if before[a]}
        ok &= s1 == 1 and {k: v[1] for k, v in diff.items()} == want
        ok &= all(a in (LIT_A, LIT_A + 1, LIT_B, LIT_B + 1) for a, _ in sfi.written)
    else:
        ok &= not diff and not sfi.written and s1 == s0
    print("   " + ("ok" if ok else "FAILED"))
    return ok, bytes(sfi.flash)


def st(mu, i): return struct.unpack("<I", mu.mem_read(ST + 4 * i, 4))[0]


def part_b(flash, title, expect_stall, nudge=False):
    mu = new_mu()
    sfi = Sfi(mu, flash, 0)
    rx, tx = bytearray(), bytearray()
    MAGIC = 0x1FFFF000
    mu.mem_write(MAGIC, b"\xfe\xe7")                       # b .

    def rd(mu, acc, addr, size, val, _):
        if addr == UART + 0x0C:
            lvl = min(len(rx), 16)
            mu.mem_write(addr, struct.pack("<I", (lvl << 4) | (0x40C if lvl else 0)))
        elif addr == UART + 0x00: mu.mem_write(addr, struct.pack("<I", rx.pop(0) if rx else 0))

    def wr(mu, acc, addr, size, val, _):
        if addr == UART + 0x00: tx.append(val & 0xFF)

    mu.hook_add(UC_HOOK_MEM_READ, rd, begin=UART, end=UART + 0xFF)
    mu.hook_add(UC_HOOK_MEM_WRITE, wr, begin=UART, end=UART + 0xFF)
    sp, reset = struct.unpack_from("<2I", flash, 0)
    mu.reg_write(UC_ARM_REG_SP, sp)
    pc = [reset]
    ticks = [0]

    def u32(a): return struct.unpack("<I", mu.mem_read(a, 4))[0]

    def run(n):
        mu.emu_start(pc[0] | 1, 0, count=n)
        pc[0] = mu.reg_read(UC_ARM_REG_PC)

    def irq(vec):
        """Run the handler for exception number `vec` to completion, as an interrupt would."""
        while mu.reg_read(UC_ARM_REG_XPSR) & 0x0600FC00: run(1)       # not inside an IT block
        saved = [mu.reg_read(r) for r in REGS]
        handler = u32(4 * vec)                                         # vector table is in flash too
        mu.reg_write(UC_ARM_REG_LR, MAGIC | 1)
        mu.reg_write(UC_ARM_REG_SP, (saved[13] - 0x20) & ~7)
        try:
            mu.emu_start(handler | 1, MAGIC, count=200000)
        except Exception as e:
            print(f"   !! exception {vec}: {e} at pc={mu.reg_read(UC_ARM_REG_PC):#x} r0={mu.reg_read(UC_ARM_REG_R0):#x} "
                  f"r1={mu.reg_read(UC_ARM_REG_R1):#x} main pc={saved[15]:#x} window={sfi.window} ticks={ticks[0]}")
            raise
        finally:
            done = mu.reg_read(UC_ARM_REG_PC) == MAGIC
            for r, v in zip(REGS, saved): mu.reg_write(r, v)
        return done

    def slice_():
        """One tick: 3000 instructions, interrupts looked at every 100 (the UART ones fire at once on hardware)."""
        for k in range(30):
            run(100)
            for _ in range(64):                    # a pending interrupt is taken as soon as the main code unmasks
                if not mu.reg_read(UC_ARM_REG_PRIMASK) & 1: break
                run(1)
            else: continue
            if k == 29 and u32(0xE000E010) & 3 == 3:
                ticks[0] += 1
                assert irq(15), "SysTick handler did not return"
            if rx and u32(UART + 0x08) & 0x0E: assert irq(22), "UART rx handler did not return"
            if u32(UART + 0x08) & 0x20: assert irq(21), "UART tx handler did not return"

    in_loop = lambda: 0x4EC <= pc[0] < 0x5CA or 0x2DE <= pc[0] < 0x2E6 or 0x8D8 <= pc[0] < 0x8EE or 0xB34 <= pc[0] < 0xB54
    in_delay = lambda: 0x20000872 <= pc[0] < 0x20000886

    print(f"-- {title}")
    reached = False
    win_ticks = 0
    for i in range(4000):
        slice_()
        if sfi.window: win_ticks += 1 if u32(0xE000E010) & 3 == 3 else 0
        if sfi.window and win_ticks > 300: break
        if not sfi.window and sfi.window_seen and in_loop(): reached = True; break
    c_old, c_bad, c_new = u32(0x20000DC0), u32(0x20000D40), u32(0x20000000)
    print(f"   after {ticks[0]} ticks: pc={pc[0]:#x} {'(12-tick wait in flash init)' if in_delay() else '(command loop)' if reached else ''} "
          f"read fault {'ACTIVE' if sfi.window else 'over'}; [0x20000dc0]={c_old:#x} [0x20000d40]={c_bad:#x} [0x20000000]={c_new:#x}")
    stalled = sfi.window and in_delay()
    ok = stalled == expect_stall
    if stalled:
        print(f"   STALL: {win_ticks} ticks delivered inside the wait, counter read by the wait did not move")
        if nudge:
            mu.mem_write(0x20000DC0, struct.pack("<I", (c_old + 1000) & 0xFFFFFFFF))
            for i in range(2000):
                slice_()
                if not sfi.window and in_loop(): reached = True; break
            print(f"   after a J7-style nudge (counter +1000): pc={pc[0]:#x} read fault {'ACTIVE' if sfi.window else 'over'} "
                  f"-> {'command loop reached' if reached else 'still stuck'}")
            ok &= reached
    if reached:
        # BEGIN for the application region: kind 0 (-> 0x4000), 1 packet, 0x100 bytes. It only sets counters.
        pl = struct.pack("<HHHHI", 0x3001, 12, 0, 1, 0x100)
        tx.clear(); rx.extend(frame(pl))
        for i in range(400):
            slice_()
            if len(tx) >= 4 and not rx: break
        for i in range(50): slice_()
        print(f"   BEGIN 0x3001 (kind 0) -> reply {bytes(tx).hex() or 'none'}  "
              f"flash commands: {[f'{c:02x}@{a:x}' for c, a, n in sfi.log if c in (0x20, 0x32)] or 'none'}")
        ok &= bytes(tx) == ACK
    print("   " + ("ok" if ok else "FAILED"))
    return ok


def main():
    if len(sys.argv) != 3: sys.exit(__doc__)
    img, res = sys.argv[1], Path(sys.argv[2])
    resident = res.read_bytes()
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([sys.executable, str(ROOT / "tools/htfw.py"), "unpack", img, d], check=True,
                       stdout=subprocess.DEVNULL)
        sec = (Path(d) / "section_00_type_0x63.bin").read_bytes()
    ok = True
    print("PART A: patcher inside our DSP firmware")
    a, patched = part_a(sec, resident, "known resident, not patched")
    ok &= a
    a, p2 = part_a(sec, resident, "strict: mapped flash readable only with the SFI+0x30 value from before", strict=True)
    ok &= a and p2 == patched
    a, p5 = part_a(sec, resident, "flash commands only run while SFI+0x10 == 0 (the resident's update-mode state)", need10=True)
    ok &= a and p5 == patched

    def half(f): f[LIT_A:LIT_A + 2] = b"\x00\x00"
    def half2(f): f[LIT_B] = 0x40
    def other(f): f[0x300] ^= 0x10
    def weird(f): f[LIT_A + 2] = 0x01
    a, p3 = part_a(sec, resident, "half done earlier (0xb60 already patched)", mutate=half); ok &= a and p3 == patched
    a, p4 = part_a(sec, resident, "interrupted inside a byte (0x16dc = 40 0d)", mutate=half2); ok &= a and p4 == patched
    a, _ = part_a(sec, resident, "different resident (one other byte differs): must refuse", mutate=other, expect_change=False); ok &= a
    a, _ = part_a(sec, resident, "literal holds something else: must refuse", mutate=weird, expect_change=False); ok &= a

    print("\nPART B: resident in update mode, read fault from flash reset until flash init is through")
    stock = bytes(resident[:FLASH_LEN].ljust(FLASH_LEN, b"\xff"))
    ok &= part_b(stock, "stock resident", expect_stall=True, nudge=True)
    ok &= part_b(patched, "patched resident (output of part A)", expect_stall=False)
    print("\nALL OK" if ok else "\nFAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
