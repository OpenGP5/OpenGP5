#!/usr/bin/env python3
"""
Set / read the granular delay + micro-looper (v3) on the pedal (firmwares/granular-delay), through
gp5_link.py's raw bridge (link commands 0x7F10 set, 0x7F11 get).

  gp5_granular.py get
  gp5_granular.py set mode=1 delay=11025 speed=-256 ...
  gp5_granular.py preset <name>          names: see PRESETS
Times are in engine samples (44100 per second at clock 1). speed is Q8: 256 = 1x, 512 = octave up,
128 = octave down, negative = reverse.
"""
import re, struct, subprocess, sys
from pathlib import Path

CMD = Path(__file__).resolve().parent / "gp5_link.py"
NAMES = ["mode", "delay", "size", "speed", "spray", "feedback", "mix", "clock", "loop", "freeze", "chaos", "looplv", "lspeed"]
SIGNED = ("speed", "lspeed")
PRESETS = {
    "off":      dict(mode=0, freeze=0),
    "ambient":  dict(mode=1, delay=13230, size=4410, speed=256, spray=1764, feedback=110, chaos=64, clock=1),
    "delay":    dict(mode=1, delay=11025, size=4410, speed=256, spray=0, feedback=96, chaos=0, clock=1),
    "shimmer":  dict(mode=1, delay=11025, size=3000, speed=512, spray=1500, feedback=120, chaos=40, clock=1),
    "reverse":  dict(mode=1, delay=8000, size=8000, speed=-256, spray=500, feedback=80, chaos=0, clock=1),
    "cloud":    dict(mode=1, delay=6000, size=2200, speed=256, spray=11000, feedback=150, chaos=200, clock=1),
    "lofi":     dict(mode=1, delay=5000, size=1500, speed=256, spray=800, feedback=150, chaos=80, clock=4),
    "freeze":   dict(mode=1, freeze=1),
    "unfreeze": dict(freeze=0),
}

def raw(hexs):
    return subprocess.run([sys.executable, str(CMD), "raw", hexs], capture_output=True, text=True).stdout

def setp(name, value):
    out = raw(struct.pack("<HHIi", 0x7F10, 12, NAMES.index(name), int(value)).hex())
    if "ACK" not in out: sys.exit(f"{name}={value}: no ACK\n{out}")

def get():
    out = raw("117f0400")
    m = re.search(r"reply\s+((?:[0-9a-f]{2} ?)+)", out)
    if not m: sys.exit("no reply\n" + out)
    b = bytes.fromhex(m.group(1).replace(" ", ""))[4:]
    v = list(struct.unpack("<20I", b[:80]))
    for n in SIGNED: v[NAMES.index(n)] = struct.unpack("<i", struct.pack("<I", v[NAMES.index(n)]))[0]
    print(" ".join(f"{n}={x}" for n, x in zip(NAMES, v)),
          f"| write {v[13]}/{v[14]} grains {v[15]} refused {v[16]} | cycles last {v[17]} worst {v[18]} late {v[19]}")

def main():
    a = sys.argv[1:]
    if a[:1] == ["get"]: return get()
    if a[:1] == ["preset"]: kv = PRESETS[a[1]]
    elif a[:1] == ["set"]: kv = dict(x.split("=") for x in a[1:])
    else: sys.exit(__doc__)
    for k, v in kv.items(): setp(k, v)
    get()

if __name__ == "__main__":
    main()
