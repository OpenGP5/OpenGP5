#!/usr/bin/env python3
"""Checks of the simulator (tools/simulate.py) on the tremolo: a script's controls, parameters, presets, saving
between runs, LEDs, screens and sound. python3 tools/test_simulate.py (needs cc)"""
import math, pathlib, subprocess, sys, tempfile, wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


def run(d, name, script, *extra):
    (d / f"{name}.txt").write_text(script)
    r = subprocess.run([sys.executable, str(ROOT / "tools/simulate.py"), "tremolo", "--seconds", "2", "--script", str(d / f"{name}.txt"),
                        "--out", str(d / f"{name}.wav"), "--shots", str(d / name), *extra], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr)
    return r


def level(path):
    with wave.open(str(path)) as w:
        b = w.readframes(w.getnframes())
    s = [int.from_bytes(b[i:i + 3], "little", signed=True) for i in range(0, len(b), 6)]
    m = s[len(s) // 4:]                                                    # past the start
    return 10 * math.log10(sum(x * x for x in m) / len(m) / 2 ** 46 + 1e-20), s


with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    r = run(d, "plain", "0.5 shot start\n")
    check(r.returncode == 0, "runs")
    check((d / "plain/start.png").exists() or (d / "plain/start.ppm").exists(), "a shot at a time")
    check((d / "plain/end.png").exists() or (d / "plain/end.ppm").exists(), "the end screen")
    check("LEDs: mint" in r.stderr, "LEDs reported")
    r = run(d, "off", "0.2 foot\n")
    check("LEDs: dim red" in r.stderr, "the foot switch reaches the firmware")
    _, dry = level(d / "off.wav")
    _, wet = level(d / "plain.wav")
    check(any(abs(a - b) > 1000 for a, b in zip(dry[44100:], wet[44100:])), "bypass changes the sound")
    run(d, "deep", "0 set Depth 100\n0 set Shape Square\n")
    _, deep = level(d / "deep.wav")
    check(sum(1 for v in deep[44100:] if v == 0) > 1000, "set Depth 100 + Square: silent half-waves")
    r = run(d, "bad", "0 set Nope 1\n")
    check(r.returncode != 0 and "no parameter Nope" in r.stderr, "an unknown parameter is refused")
    store = d / "store.bin"
    run(d, "save", "0.1 set Rate 900\n0.2 preset save 3\n", "--store", str(store))
    r = run(d, "load", "0.1 set Rate 100\n0.2 preset load 3\n0.5 shot loaded\n", "--store", str(store))
    check(r.returncode == 0 and store.exists(), "the store is kept between runs")
    run(d, "restart", "0.5 shot after\n", "--store", str(store))
    check((d / "restart/after.png").exists() or (d / "restart/after.ppm").exists(), "a restart with the store")
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
