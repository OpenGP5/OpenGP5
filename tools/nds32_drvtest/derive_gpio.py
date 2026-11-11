#!/usr/bin/env python3
"""
Turn a vendor register-difference log (run.sh ... vendor) into the effect of each scenario:
which bits of which register it forces to 0, forces to 1, or toggles/copies.

  derive_gpio.py reference/gpio_sweep_vendor.txt

For a call that only clears and sets fixed bits, seed 0 (all registers 0) shows the bits it sets and
seed 1 (all 1) the bits it clears; the four random seeds are then predicted and compared. A scenario
that does not fit that model is reported as such.
"""
import re, sys
PAT = lambda a, s: 0 if s == 0 else 0xFFFFFFFF if s == 1 else None
def pattern(addr, seed):
    if seed == 0: return 0
    if seed == 1: return 0xFFFFFFFF
    x = ((addr * 2654435761) ^ (seed * 0x9E3779B9)) & 0xFFFFFFFF
    x ^= x >> 15; x = x * 0x2C1B3C6D & 0xFFFFFFFF; x ^= x >> 12; x = x * 0x297A2D39 & 0xFFFFFFFF; x ^= x >> 15
    return x
log = {}
for line in open(sys.argv[1]):
    m = re.match(r"== (\S+) seed (\d+)", line)
    if m: cur = log.setdefault(m.group(1), {}).setdefault(int(m.group(2)), {}); continue
    m = re.match(r"([0-9a-f]{8}): ([0-9a-f]{8}) -> ([0-9a-f]{8})", line)
    if m: cur[int(m.group(1), 16)] = int(m.group(3), 16)
for name, seeds in log.items():
    addrs = sorted({a for s in seeds.values() for a in s})
    out, ok = [], True
    for a in addrs:
        setm = seeds[0].get(a, 0)
        clrm = ~seeds[1].get(a, 0xFFFFFFFF) & 0xFFFFFFFF
        for s in (2, 3, 4, 5):
            before = pattern(a, s); after = seeds[s].get(a, before)
            if after != ((before & ~clrm) | setm) & 0xFFFFFFFF: ok = False
        out.append(f"{a:08x} clear {clrm:08x} set {setm:08x}")
    print(f"{name:14s} {'ok ' if ok else 'NOT-A-MASK'}  " + "; ".join(out))
