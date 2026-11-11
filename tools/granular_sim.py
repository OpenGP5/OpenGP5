#!/usr/bin/env python3
"""
Host check of the granular delay / micro-looper (firmwares/granular-delay/dsp/granular.h).

  granular_sim.py            run the checks
  granular_sim.py --demo DIR also write demo WAVs (a plucked-note phrase through a few settings)

Builds firmwares/granular-delay/dsp/test/granular_host.c with the host compiler and feeds it samples in the
pedal's format (16-bit value << 16, blocks of 64).
"""
import array, math, random, subprocess, sys, tempfile, wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmwares/granular-delay/dsp/test/granular_host.c"
FS = 44100
MODE, DELAY, SIZE, SPEED, SPRAY, FB, MIX, CLOCK, LOOP, FREEZE, CHAOS, LOOPLV, LSPEED = range(13)
EXACT = (CHAOS, 0, SPRAY, 0, FB, 0, MIX, 256, LOOPLV, 256)   # nothing random, effect only

def build(d):
    exe = Path(d) / "gr"
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(SRC), "-o", str(exe)], check=True)
    return exe

def run(exe, x16, *args):
    """x16: int16-range samples. args: ints, or '@N' strings to switch settings at sample N.
    Returns left and right output as 16-bit-range ints, the raw 32-bit left output, and the harness note."""
    inp = array.array("i", [v << 16 for v in x16]).tobytes()
    r = subprocess.run([str(exe), *[str(a) for a in args]], input=inp, capture_output=True, check=True)
    y = array.array("i"); y.frombytes(r.stdout)
    return [v >> 16 for v in y[0::2]], [v >> 16 for v in y[1::2]], y[0::2], r.stderr.decode().strip()

def sine(n, f, amp=12000):
    return [round(amp * math.sin(2 * math.pi * f * i / FS)) for i in range(n)]

def freq(y):
    m = sum(y) / len(y)
    return sum(1 for a, b in zip(y, y[1:]) if a < m <= b) * FS / len(y)

def maxerr(a, b):
    return max(abs(p - q) for p, q in zip(a, b))

def peak(y):
    return max(abs(v) for v in y)

def pluck_phrase(seconds=6.0):
    rng = random.Random(5)
    out = [0.0] * int(seconds * FS)
    notes = [(0.0, 196.0), (0.45, 246.9), (0.9, 293.7), (1.35, 392.0), (2.0, 329.6), (2.5, 246.9)]
    for t0, f in notes:                      # Karplus-Strong
        n = int(FS / f); buf = [rng.uniform(-1, 1) for _ in range(n)]; s = int(t0 * FS)
        for i in range(min(int(1.6 * FS), len(out) - s)):
            k = i % n
            out[s + i] += buf[k]; buf[k] = 0.996 * 0.5 * (buf[k] + buf[(k + 1) % n])
    pk = max(abs(v) for v in out)
    return [round(v / pk * 14000) for v in out]

def wav(path, l, r):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(FS)
        w.writeframes(array.array("h", [max(-32768, min(32767, v)) for p in zip(l, r) for v in p]).tobytes())

def main():
    ok = True
    def check(name, cond, info=""):
        nonlocal ok
        ok &= bool(cond)
        print(f"  {'ok ' if cond else 'FAIL'} {name} {info}")
    with tempfile.TemporaryDirectory() as d:
        exe = build(d)
        x = sine(3 * FS, 220.0)
        l, r, raw, _ = run(exe, x)
        check("off: both outputs are the input, bit-exact", list(raw) == [v << 16 for v in x] and l == r)

        D = 5000
        l, r, _, st = run(exe, x, MODE, 1, DELAY, D, SIZE, 2000, SPEED, 256, *EXACT)
        lat, err = min(((k, maxerr(l[20000:], x[20000 - D - k:])) for k in range(0, 5)), key=lambda t: t[1])
        check("delay 1x, effect only = input delayed by DELAY + latency", err <= 4 and l == r,
              f"latency {lat}, max err {err} of 12000; {st}")
        dl = lambda sig, t0: sig[t0 - D - lat:]                    # the delayed copy from sample t0 on

        l, _, _, _ = run(exe, x, MODE, 1, DELAY, D, SIZE, 2000, SPEED, 256, *EXACT, MIX, 128)
        err = maxerr(l[20000:], [a + b for a, b in zip(x[20000:], dl(x, 20000))])
        check("mix 128 = dry + delayed, both at full level", err <= 4, f"max err {err}")
        l, _, _, _ = run(exe, x, MODE, 1, DELAY, D, SIZE, 2000, SPEED, 256, *EXACT, MIX, 192)
        err = maxerr(l[20000:], [a // 2 + b for a, b in zip(x[20000:], dl(x, 20000))])
        check("mix 192 = dry at half level + delayed at full level", err <= 4, f"max err {err}")

        l, _, _, _ = run(exe, x, MODE, 1, DELAY, 8000, SIZE, 4000, SPEED, 512, *EXACT)
        f = freq(l[40000:])
        check("speed 2x plays an octave up", abs(f - 440) < 12, f"{f:.1f} Hz from 220 Hz")
        l, _, _, _ = run(exe, x, MODE, 1, DELAY, 8000, SIZE, 4000, SPEED, 128, *EXACT)
        f = freq(l[40000:])
        check("speed 0.5x plays an octave down", abs(f - 110) < 6, f"{f:.1f} Hz")
        l, _, _, st = run(exe, x, MODE, 1, DELAY, 8000, SIZE, 4000, SPEED, -256, *EXACT)
        f = freq(l[40000:]); pk = peak(l[40000:])
        check("reverse grains keep the pitch and level", abs(f - 220) < 8 and 6000 < pk < 20000, f"{f:.1f} Hz peak {pk}; {st}")

        # freeze: 1 s of 220 Hz, 1 s of 330 Hz, freeze, then 2 s of 550 Hz played over the loop
        Lp = 8000
        x2 = sine(FS, 220.0, 6000) + sine(FS, 330.0, 6000) + sine(2 * FS, 550.0, 6000)
        base = (MODE, 1, DELAY, D, SIZE, 2000, SPEED, 256, *EXACT, LOOP, Lp)
        l, r, _, st = run(exe, x2, *base, f"@{2*FS}", FREEZE, 1)
        t0 = 2 * FS + D + 3 * Lp
        live = dl(x2, t0)
        loop = [a - b for a, b in zip(l[t0:], live)]               # what is left after taking the live delay away
        per = maxerr(loop[Lp:3 * Lp], loop[:2 * Lp]); f = freq(loop[:4 * Lp]); pk = peak(loop)
        check("freeze: the delay keeps working on the live input", peak(live[:FS // 2]) > 5000 and l == r)
        check("freeze: under it a loop of the last note repeats with period LOOP", per <= 6 and abs(f - 330) < 12 and pk > 2000,
              f"period err {per}, {f:.1f} Hz, peak {pk}; {st}")
        l0, _, _, _ = run(exe, x2, *base, LOOPLV, 128, f"@{2*FS}", FREEZE, 1)
        loop0 = [a - b for a, b in zip(l0[t0:], live)]
        check("loop level 128 halves the loop only", maxerr(loop0[:2 * Lp], [v // 2 for v in loop[:2 * Lp]]) <= 6)
        l, _, _, _ = run(exe, x2, *base, LSPEED, 128, f"@{2*FS}", FREEZE, 1)
        loop = [a - b for a, b in zip(l[t0:], live)]
        per = maxerr(loop[2 * Lp:4 * Lp], loop[:2 * Lp]); f = freq(loop[:4 * Lp])
        check("loop at half speed: octave down, twice as long", per <= 6 and abs(f - 165) < 8, f"period err {per}, {f:.1f} Hz")
        l, _, _, _ = run(exe, x2, *base, LSPEED, -256, f"@{2*FS}", FREEZE, 1)
        loop = [a - b for a, b in zip(l[t0:], live)]
        per = maxerr(loop[Lp:3 * Lp], loop[:2 * Lp]); f = freq(loop[:4 * Lp])
        check("loop backwards: same period and pitch", per <= 6 and abs(f - 330) < 12, f"period err {per}, {f:.1f} Hz")
        x3 = sine(FS, 220.0, 6000) + sine(FS, 330.0, 6000) + [0] * (2 * FS)
        l, _, _, _ = run(exe, x3, *base, f"@{2*FS}", FREEZE, 1, f"@{3*FS}", FREEZE, 0)
        check("unfreeze: the loop stops, silence returns", peak(l[3 * FS - 2000:3 * FS]) > 1500 and peak(l[-FS // 2:]) <= 2,
              f"loop peak before {peak(l[3 * FS - 2000:3 * FS])}, tail {peak(l[-FS // 2:])}")

        l, _, _, _ = run(exe, x, MODE, 1, DELAY, 2000, SIZE, 1000, SPEED, 256, *EXACT, CLOCK, 4)
        f = freq(l[60000:]); pk = peak(l[60000:])
        check("clock /4: same pitch, still at level", abs(f - 220) < 8 and pk > 8000, f"{f:.1f} Hz peak {pk}")

        l, r, _, st = run(exe, x, MODE, 1, DELAY, 6000, SIZE, 2000, SPEED, 256, SPRAY, 4000, FB, 100, MIX, 256, CHAOS, 256)
        nd = int(st.split()[-1]); ns = int(st.split()[1])
        check("chaos: left and right differ, sound present, few grains refused", l != r and peak(l[40000:]) > 3000
              and peak(r[40000:]) > 3000 and nd * 20 < ns, st)
        l1, r1, _, _ = run(exe, x, MODE, 1, DELAY, 6000, SIZE, 2000, SPEED, 256, SPRAY, 4000, FB, 100, MIX, 256, CHAOS, 0)
        check("chaos 0: left = right", l1 == r1)

        rng = random.Random(1)
        n = [rng.randint(-32768, 32767) for _ in range(4 * FS)]
        runs = 0
        for sp in (1024, -1024, 16, -16, 300):
            for sz in (64, 8192):
                for ch in (0, 256):
                    run(exe, n, MODE, 1, DELAY, 62000, SIZE, sz, SPEED, sp, LSPEED, -sp, SPRAY, 31232, FB, 250, MIX, 256,
                        CHAOS, ch, LOOP, 99999, f"@{FS}", FREEZE, 1, f"@{2*FS}", LOOP, 256, CLOCK, 3, f"@{3*FS}", FREEZE, 0,
                        DELAY, 0, LSPEED, sp)
                    runs += 1
        check("extreme settings with full-scale noise: no crash, no out-of-bounds (ASan/UBSan)", runs == 20)

        if "--demo" in sys.argv:
            out = Path(sys.argv[sys.argv.index("--demo") + 1]); out.mkdir(parents=True, exist_ok=True)
            ph = pluck_phrase(9.0)
            demos = {
                "1_default_ambient": (MODE, 1),
                "2_more_chaos": (MODE, 1, CHAOS, 200, SPRAY, 8000, FB, 150),
                "3_freeze_play_over": (MODE, 1, f"@{int(1.8*FS)}", FREEZE, 1),
                "4_freeze_half_speed_chaos": (MODE, 1, CHAOS, 160, LSPEED, 128, f"@{int(1.8*FS)}", FREEZE, 1),
                "5_lofi_clock_div2_shimmer": (MODE, 1, CLOCK, 2, SPEED, 512, CHAOS, 120, FB, 140),
            }
            for name, args in demos.items():
                l, r, _, _ = run(exe, ph, *args); wav(out / f"{name}.wav", l, r)
            print("demo WAVs in", out)
    print("ALL OK" if ok else "FAILED")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
