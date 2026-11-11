#!/usr/bin/env python3
"""Boot a built image's DSP firmware in an emulator and check the platform contract, no pedal needed.

  python3 tools/dsp_emu_check.py build/GP-5_<name>.bin [--set ID=VALUE ...]

Checks: it starts (no fault), answers the version query the control half waits for, takes an output level,
processes audio blocks, its outputs are not silent for a test tone, and settings are acknowledged.
Needs `pip install unicorn`. Timing is not modelled: use GP5_CYCLES_* on the pedal for that.
"""
import argparse, math, struct, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gp5tool

UART, ADMA, ST = 0x40005000, 0x40030000, 0x20007F00
IN_A, IN_B, OUT_L, OUT_R = 0x1001F400, 0x1001F600, 0x1001F800, 0x1001FA00


def crc8(p):
    c = 0
    for b in p:
        c ^= b
        for _ in range(8): c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("image")
    ap.add_argument("--set", action="append", default=[], metavar="ID=VALUE")
    a = ap.parse_args()
    sec = dict(gp5tool.inspect(open(a.image, "rb").read())[0]).get(0x63)
    if sec is None: sys.exit("the image has no dsp section")
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for base, size in ((0, 0x200000), (0x10000000, 0x20000), (0x20000000, 0x8000),
                       (0x40000000, 0x100000), (0x50000000, 0x1000), (0xE0000000, 0x100000)):
        mu.mem_map(base, size)
    mu.mem_write(0x4000, sec)
    rx, tx = bytearray(), bytearray()

    def rd(mu, acc, addr, size, val, _):
        if addr == UART + 0x0C: mu.mem_write(addr, struct.pack("<I", min(len(rx), 31) << 4))
        elif addr == UART + 0x00: mu.mem_write(addr, struct.pack("<I", rx.pop(0) if rx else 0))
        elif addr == ADMA + 0x00: mu.mem_write(addr, struct.pack("<I", 1))
        elif addr == ADMA + 0x04: mu.mem_write(addr, struct.pack("<I", 0xF))

    def wr(mu, acc, addr, size, val, _):
        if addr == UART + 0x00: tx.append(val & 0xFF)
    mu.hook_add(UC_HOOK_MEM_READ, rd, begin=0x40000000, end=0x400FFFFF)
    mu.hook_add(UC_HOOK_MEM_WRITE, wr, begin=0x40000000, end=0x400FFFFF)
    sp, reset = struct.unpack_from("<2I", sec, 0x400)
    mu.reg_write(UC_ARM_REG_SP, sp)
    pc = [reset]

    def run(n):
        mu.emu_start(pc[0] | 1, 0, count=n); pc[0] = mu.reg_read(UC_ARM_REG_PC)

    def st(i): return struct.unpack("<I", mu.mem_read(ST + 4 * i, 4))[0]

    def send(pl, n=300000):
        tx.clear(); ln = len(pl) + 4
        rx.extend(bytes([crc8(pl), ln & 0xFF, ln >> 8, 1]) + pl); run(n); return bytes(tx)

    ok = True

    def check(name, cond, info=""):
        nonlocal ok; ok &= bool(cond); print(f"  {'ok ' if cond else 'FAIL'} {name} {info}")

    tone = [int(0.5 * 2 ** 31 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(128)]
    run(3_000_000)                                   # start-up clears the audio buffers: load the tone after it
    mu.mem_write(IN_A, struct.pack("<128i", *tone)); mu.mem_write(IN_B, struct.pack("<128i", *[0] * 128))
    check("starts without a fault", st(3) == 0, hex(st(3)) if st(3) else "")
    r = send(bytes.fromhex("0060080000000000"))
    check("answers the version query", len(r) >= 16 and r[4:8] == bytes.fromhex("00600c00"),
          "version 0x%08x" % struct.unpack_from("<I", r, 12)[0] if len(r) >= 16 else r.hex())
    check("takes the output level", send(bytes.fromhex("01100c00000000000000803f")) == b"\x01\x04\x00\x14")
    for s in a.set:
        k, _, v = s.partition("=")
        check(f"setting {k} = {v} acknowledged",
              send(struct.pack("<HHIi", 0x7F10, 12, int(k, 0), int(v, 0))) == b"\x01\x04\x00\x14")
    blocks0 = st(13); run(3_000_000)
    outl = struct.unpack("<128i", mu.mem_read(OUT_L, 512)); outr = struct.unpack("<128i", mu.mem_read(OUT_R, 512))
    check("processes audio blocks", st(13) > blocks0, f"{st(13) - blocks0} blocks")
    check("outputs are not silent for a tone", max(map(abs, outl)) > 0 and max(map(abs, outr)) > 0,
          f"peak L {max(map(abs, outl)) / 2 ** 31:.3f} R {max(map(abs, outr)) / 2 ** 31:.3f}")
    check("still no fault", st(3) == 0 and st(8) == 0, hex(st(3)) if st(3) else "")
    print("ALL OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
