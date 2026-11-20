#!/usr/bin/env python3
"""Run a whole firmware on the computer: both processors' code, the audio link between them, the screen, the controls.
No pedal needed (docs/platform/simulator.md).

  python3 tools/simulate.py <firmware> [--in guitar.wav | --tone] [--out out.wav] [--script controls.txt]
                            [--seconds S] [--shots DIR] [--knob 0..255] [--store FILE] [--files DIR]

<firmware> is a folder under firmwares/, or the path of a firmware folder elsewhere (it needs a firmware.json).
  --in WAV        the guitar (16 or 24-bit PCM, first channel); --tone: a plucked test tone instead (default)
  --out WAV       what the pedal puts out, stereo 24-bit (default build/sim/<firmware>.wav)
  --script FILE   what you do, one line each, a time in seconds first (# starts a comment):
                      0.5  foot                  press and let go of the foot switch (foot down / foot up: one edge)
                      1.0  para turn -2          turn PARA two clicks back
                      1.5  para click            push and let go (para hold: held 0.8 s; para down / para up)
                      2.0  knob 128              the volume knob, 0..255
                      2.5  set Rate 800          a parameter, as a computer sets it: by the name shown (Loop_level for
                                                 "Loop level") or params.h's; a choice by name: set Shape Square
                      3.0  preset load 2         presets as on the pedal, from 1 (preset save 2)
                      3.5  shot after-load       the screen now, as <shots>/after-load.png
  --seconds S     how long (default: the input's length; 4 s for the tone)
  --shots DIR     where screens go (default build/sim/<firmware>/); the screen at the end is end.png
  --knob N        where the volume knob starts (default 255)
  --store FILE    keep the firmware's saved settings in FILE between runs (default: start empty every time)
  --files DIR     the firmware's files (gp5_file): every file in DIR, by its name
  --check         build with the address and undefined-behaviour checkers: a crash or a bad memory access stops
                  with the line it happened on (slower)
The log (gp5_log on either processor) and the LEDs print as the run goes. The timing is the pedal's: 64-sample
blocks, fw_tick every 10 ms, the PARA hold at 0.6 s, the audio link's words three blocks on. The arithmetic is the
pedal's maths code compiled for the computer; cycle counts are not simulated (gp5_link.py status on the pedal).
Needs a C compiler (cc); Pillow for PNG screens (else they stay .ppm).
"""
import argparse, array, math, os, pathlib, re, struct, subprocess, sys, tempfile, wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "platform"))
import manifest                                         # noqa: E402
from run_effect import read_wav, tone                   # noqa: E402


def firmware_dir(name):
    p = pathlib.Path(name)
    return p if (p / "firmware.json").exists() else ROOT / "firmwares" / name


def param_names(fw):
    """params.h: names in order (their ids) and each choice's names"""
    if not (fw / "params.h").exists(): return [], {}, {}
    t = (fw / "params.h").read_text()
    entries = re.findall(r'\b[PC]\(\s*([A-Z0-9_]+)\s*,\s*"([^"]*)"', t)
    names = [k for k, _ in entries]
    shown = {d.upper().replace(" ", "_"): k for k, d in entries}       # "Loop level" -> LOOP_LEVEL
    choices = {k: c.split("|") for k, c in re.findall(r'\bC\(\s*([A-Z0-9_]+)\s*,\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*\d+\s*,\s*"([^"]*)"', t)}
    return names, choices, shown


def events(script, names, choices, shown):
    """the script's lines -> (ms, event) pairs, sorted"""
    out = []
    for n, raw in enumerate(script.splitlines(), 1):
        line = raw.split("#", 1)[0].split()
        if not line: continue
        def bad(why): sys.exit(f"script line {n}: {why}: {raw.strip()}")
        try: t = round(float(line[0]) * 1000)
        except ValueError: bad("starts with a time in seconds")
        w = [x.lower() for x in line[1:]]
        if w[:1] == ["foot"]:
            if len(w) == 1: out += [(t, "foot_down"), (t + 100, "foot_up")]
            elif w[1] in ("down", "up"): out.append((t, f"foot_{w[1]}"))
            else: bad("foot, foot down or foot up")
        elif w[:1] == ["para"] and len(w) >= 2:
            if w[1] == "click": out += [(t, "para_down"), (t + 100, "para_up")]
            elif w[1] == "hold": out += [(t, "para_down"), (t + 800, "para_up")]
            elif w[1] in ("down", "up"): out.append((t, f"para_{w[1]}"))
            elif w[1] == "turn" and len(w) == 3 and re.match(r"^[+-]?\d+$", w[2]): out.append((t, f"turn {int(w[2])}"))
            else: bad("para click | hold | down | up | turn N")
        elif w[:1] == ["knob"] and len(w) == 2 and w[1].isdigit(): out.append((t, f"knob {int(w[1])}"))
        elif w[:1] == ["set"] and len(w) == 3:
            k, v = line[2].upper(), line[3]
            k = k if k in names else shown.get(k, k)                         # the code's name or the one shown
            if k in names:
                low = [c.lower() for c in choices.get(k, [])]
                if v.lower() in low: v = str(low.index(v.lower()))
                k = names.index(k)
            elif re.match(r"^\d+$", k): k = int(k)
            else: bad(f"no parameter {line[2]} (params.h has {', '.join(names) or 'none'}; or their names as shown, spaces as _)")
            try: out.append((t, f"set {k} {int(v, 0)}"))
            except ValueError: bad("set NAME VALUE (a number, or a choice's name)")
        elif w[:1] == ["preset"] and len(w) == 3 and w[1] in ("load", "save") and w[2].isdigit() and int(w[2]) >= 1:
            out.append((t, f"preset_{w[1]} {int(w[2]) - 1}"))
        elif w[:1] == ["shot"] and len(w) == 2 and re.match(r"^[A-Za-z0-9_.-]+$", line[2]): out.append((t, f"shot {line[2]}"))
        elif w == ["storage", "changed"]: out.append((t, "storage_changed"))
        else: bad("not understood")
    return sorted(out, key=lambda e: e[0])


def build(fw, m, d, check=False):
    plat = ROOT / "platform"
    flags = manifest.flags(m, fw)
    lib, exe = d / "sim_dsp.so", d / "sim"
    cc = ["cc", "-std=gnu99", "-Wall", "-Wno-unused-function", "-Werror=implicit-function-declaration", "-fno-builtin", "-g", *flags,
          *(["-fsanitize=address,undefined", "-fno-sanitize=shift-base", "-fno-omit-frame-pointer"] if check else [])]
    subprocess.run([*cc, "-O2", "-fPIC", "-shared", "-fvisibility=hidden", "-DGP5_HOST", f"-I{fw / 'dsp'}", f"-I{fw}",
                    f"-I{plat / 'dsp'}", str(plat / "host/sim_dsp.c"), str(plat / "dsp/gp5_math.c"), "-o", str(lib)], check=True)
    ctl = sorted((fw / "nds32").glob("*.c"))
    math_inline = any('#include "gp5_math.c"' in c.read_text(errors="replace") for c in ctl)
    subprocess.run([*cc, "-O1", "-DGP5_HOST", "-DUI_PANEL", "-DGP5_CTL_SIDE", f"-I{plat / 'nds32'}", f"-I{plat / 'host/include'}",
                    f"-I{plat / 'dsp'}", f"-I{fw}", f"-I{fw / 'nds32'}", str(plat / "host/sim.c"), str(plat / "nds32/gp5_params.c"),
                    str(plat / "nds32/gp5_ui.c"), str(plat / "nds32/text.c"), *map(str, ctl),
                    *([] if math_inline else [str(plat / "dsp/gp5_math.c")]), "-lm", *(["-ldl"] if sys.platform.startswith("linux") else []),
                    "-o", str(exe)], check=True)
    return lib, exe


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware")
    ap.add_argument("--in", dest="inp"); ap.add_argument("--tone", action="store_true")
    ap.add_argument("--out"); ap.add_argument("--script"); ap.add_argument("--seconds", type=float)
    ap.add_argument("--shots"); ap.add_argument("--knob", type=int, default=255)
    ap.add_argument("--store"); ap.add_argument("--files")
    ap.add_argument("--check", action="store_true", help="build with the address and undefined-behaviour checkers")
    a = ap.parse_args()
    fw = firmware_dir(a.firmware).resolve()
    try: m = manifest.load(fw)
    except manifest.ManifestError as e: sys.exit(str(e))
    if not m: sys.exit(f"{fw}: no firmware.json (the simulator runs OpenGP5 1.0 firmwares)")
    rate = m["audio"]["sample_rate"]
    names, choices, shown = param_names(fw)
    ev = [(0, f"knob {max(0, min(255, a.knob))}")] + events(pathlib.Path(a.script).read_text() if a.script else "", names, choices, shown)
    samples = read_wav(a.inp) if a.inp else tone()
    seconds = a.seconds or (len(samples) / rate)
    out = pathlib.Path(a.out or ROOT / "build/sim" / f"{m['id']}.wav")
    shots = pathlib.Path(a.shots or ROOT / "build/sim" / m["id"])
    out.parent.mkdir(parents=True, exist_ok=True); shots.mkdir(parents=True, exist_ok=True)
    for old in shots.glob("*.p[np][gm]"): old.unlink()
    with tempfile.TemporaryDirectory() as d:
        d = pathlib.Path(d)
        lib, exe = build(fw, m, d, a.check)
        (d / "events.txt").write_text("".join(f"{t} {e}\n" for t, e in ev))
        (d / "in.raw").write_bytes(samples.tobytes())
        env = dict(os.environ, ASAN_OPTIONS=os.environ.get("ASAN_OPTIONS", "abort_on_error=0"))   # (--check: report, no crash report)
        r = subprocess.run([str(exe), str(lib), str(d / "events.txt"), str(d / "in.raw"), str(d / "out.raw"), str(shots),
                            f"{seconds}", str(pathlib.Path(a.store).resolve()) if a.store else "-",
                            str(pathlib.Path(a.files).resolve()) if a.files else "-"], env=env)
        if r.returncode:
            sig = -r.returncode if r.returncode < 0 else r.returncode - 256 if r.returncode > 128 else 0
            sys.exit(f"the firmware crashed (signal {abs(sig)}): run again with --check to find where"
                     if r.returncode < 0 or r.returncode > 128 else f"the simulation failed ({r.returncode})")
        pcm = array.array("i"); pcm.frombytes((d / "out.raw").read_bytes())
    with wave.open(str(out), "wb") as w:
        w.setnchannels(2); w.setsampwidth(3); w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<i", v)[1:] for v in pcm))
    peak = max((abs(v) for v in pcm), default=0) / 2 ** 31
    try:
        from PIL import Image
        for p in sorted(shots.glob("*.ppm")):
            Image.open(p).resize((640, 320), Image.NEAREST).save(p.with_suffix(".png")); p.unlink()
        kind = "png"
    except ImportError:
        kind = "ppm (pip install pillow for PNG)"
    print(f"{m['name']} {m['version']}: {seconds:.2f} s at {rate} Hz -> {out} (peak {20 * math.log10(peak) if peak else -999:.1f} dBFS); "
          f"screens in {shots} ({kind})")


if __name__ == "__main__":
    main()
