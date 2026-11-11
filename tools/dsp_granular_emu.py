#!/usr/bin/env python3
"""
Smoke test of the granular effect inside the DSP firmware image (-D FX_GRANULAR), in Unicorn.

  dsp_granular_emu.py <htfw image>

The algorithm itself is checked on the host (tools/granular_sim.py). This checks the firmware side:
link commands 0x7F10 (set) / 0x7F11 (get), state and record buffer placement, and that the effect
changes the output only when it is switched on. Same peripheral model as tools/dsp_pass_emu.py.
"""
import struct, subprocess, sys, tempfile
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parents[1]
UART, ADMA, ST = 0x40005000, 0x40030000, 0x20007F00

def crc8(p):
    c = 0
    for b in p:
        c ^= b
        for _ in range(8): c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def main():
    with tempfile.TemporaryDirectory() as d:
        subprocess.run([sys.executable, str(ROOT / "tools/htfw.py"), "unpack", sys.argv[1], d], check=True,
                       stdout=subprocess.DEVNULL)
        sec = (Path(d) / "section_00_type_0x63.bin").read_bytes()
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for base, size in ((0, 0x200000), (0x10000000, 0x20000), (0x20000000, 0x8000),
                       (0x40000000, 0x100000), (0x50000000, 0x1000), (0xE0000000, 0x100000)):
        mu.mem_map(base, size)                     # RAM sizes as measured on the pedal: 32 KB + 128 KB
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
    def setp(i, v): return send(struct.pack("<HHII", 0x7F10, 12, i, v & 0xFFFFFFFF)) == b"\x01\x04\x00\x14"
    def get():
        r = send(struct.pack("<HH", 0x7F11, 4))
        assert r[3] == 1 and crc8(r[4:]) == r[0] and r[4:6] == b"\x11\x7f", r.hex()
        return struct.unpack("<20I", r[8:])
    ina = [((i * 0x01234567) & 0x7FFFFFF) - 0x4000000 for i in range(128)]
    inb = [-(v // 3) for v in ina]
    def load_in():
        mu.mem_write(0x1001F400, struct.pack("<128i", *ina)); mu.mem_write(0x1001F600, struct.pack("<128i", *inb))
    def outs(): return list(struct.unpack("<128i", mu.mem_read(0x1001F800, 512)))
    def outr(): return list(struct.unpack("<128i", mu.mem_read(0x1001FA00, 512)))
    dry = [(a >> 1) - (b >> 1) for a, b in zip(ina, inb)]

    ok = True
    def check(name, cond, info=""):
        nonlocal ok; ok &= bool(cond); print(f"  {'ok ' if cond else 'FAIL'} {name} {info}")
    run(3_000_000)                                 # boot: the record buffer is zeroed here
    load_in()
    r = send(bytes.fromhex("006008000000000000"[:16]))   # version query, as the control processor sends it
    check("version query answered (the control side waits for it)",
          len(r) >= 16 and r[3] == 1 and crc8(r[4:]) == r[0] and r[4:8] == bytes.fromhex("00600c00")
          and struct.unpack_from("<I", r, 12)[0] != 0, r.hex())
    check("level 1.0 ACKed", send(bytes.fromhex("01100c00000000000000803f")) == b"\x01\x04\x00\x14")
    run(400000)
    g = get()
    check("defaults reported", g[:13] == (0, 13230, 4410, 256, 1764, 110, 128, 1, 22050, 0, 64, 180, 256), str(g[:13]))
    check("off: both outputs = dry, both buffers recording", outs() == dry and outr() == dry and g[13] > 0 and g[14] > 0, f"write indexes {g[13]} {g[14]}")
    d16 = {v >> 16 for v in dry} | {0}             # 0: blocks recorded before the test input was loaded
    rec = struct.unpack("<%dh" % g[13], mu.mem_read(0x10000000, 2 * g[13]))
    check("delay recording at 0x10000000 holds the input as 16-bit samples", any(rec) and set(rec) <= d16)
    rec = struct.unpack("<%dh" % g[14], mu.mem_read(0x1000B000, 2 * g[14]))
    check("loop recording at 0x1000B000 holds the input as 16-bit samples", any(rec) and set(rec) <= d16)
    check("set commands ACKed", all([setp(1, 300), setp(2, 128), setp(6, 256), setp(5, 0), setp(4, 0), setp(10, 0), setp(0, 1)]))
    run(1_500_000)
    g = get()
    o = outs()
    check("on: params stored, grains running, none refused", g[:3] == (1, 300, 128) and g[15] > 10 and g[16] == 0,
          f"started {g[15]} refused {g[16]}")
    check("on, effect only, chaos 0: output differs from dry, is not silent, left = right",
          o != dry and max(abs(v) for v in o) > 1 << 20 and o == outr())
    check("chaos 256: left and right differ", setp(10, 256) and (run(1_500_000) or True) and outs() != outr())
    check("clamping: size 5 -> 64, mode 9 -> 1, loop 99999 -> 35840",
          setp(2, 5) and setp(0, 9) and setp(8, 99999) and (lambda q: (q[0], q[2], q[8]) == (1, 64, 35840))(get()))
    setp(9, 1); a0 = get(); run(400000); a1 = get()
    check("freeze: loop recording stops, delay recording goes on", a0[14] == a1[14] and a0[13] != a1[13] and a1[9] == 1,
          f"loop {a0[14]}, delay {a0[13]} -> {a1[13]}")
    setp(9, 0); a0 = get(); run(400000); a1 = get()
    check("unfreeze: loop recording resumes", a0[14] != a1[14])
    check("off again: output = dry", setp(0, 0) and (run(400000) or True) and outs() == dry and outr() == dry)
    check("no fault, no CRC error", st(3) == 0 and st(8) == 0)
    hi = struct.unpack("<I", mu.mem_read(0x1001E800, 4))[0]
    check("nothing written between the record buffer and the audio DMA buffers", mu.mem_read(0x1001E800, 0xC00) == bytes(0xC00), hex(hi))
    print("ALL OK" if ok else "FAILED"); sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
