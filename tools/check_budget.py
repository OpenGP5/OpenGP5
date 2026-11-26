#!/usr/bin/env python3
"""The 1.0 promise for memory: the room the platform leaves a firmware never shrinks in 1.x.

Builds compat/1.0/passthrough (a firmware of a few lines, so what it uses is almost all platform) for the pedal and
compares the platform's own share with compat/1.0/budget.json: what the platform may take for all of 1.x (its size at
1.0 plus a small reserve for fixes). Fails if it takes more, so a firmware that fit in 1.0 still fits (Dreamcatcher
leaves about 13 KB of control-processor RAM free).

  python3 tools/check_budget.py            build, then check
  python3 tools/check_budget.py LOG        check the output of an earlier platform/build_firmware.sh compat/1.0/passthrough
  python3 tools/check_budget.py --show     build and print what the platform takes now
"""
import json, pathlib, re, shutil, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUDGET = ROOT / "compat/1.0/budget.json"
WHAT = {   # name: (pattern in the build's output, what it is)
    "dsp_code":   (r"DSP responder\.c: (\d+) bytes", "DSP code and constants"),
    "dsp_vars":   (r"DSP RAM: variables (\d+) of", "DSP variables"),
    "dsp_large":  (r"GP5_LARGE (\d+) of", "DSP buffers (GP5_LARGE)"),
    "ctl_code":   (r"OK  app (\d+) bytes", "control processor code and constants"),
    "ctl_ram":    (r"^([0-9a-f]{8}) B _end$", "control processor RAM (end of variables, 0x20004000 on)"),
}


def measure(log):
    out = {}
    for k, (pat, _) in WHAT.items():
        m = re.search(pat, log, re.M)
        if not m: sys.exit(f"check_budget: no '{WHAT[k][1]}' in the build output")
        out[k] = int(m.group(1), 16) - 0x20004000 if k == "ctl_ram" else int(m.group(1))
    return out


def build():
    """The passthrough with everything the platform can add switched on (USB audio, Bluetooth): the platform's
    largest share, which is what a firmware must still have room beside."""
    with tempfile.TemporaryDirectory() as d:
        fw = pathlib.Path(d) / "passthrough"
        shutil.copytree(ROOT / "compat/1.0/passthrough", fw)
        m = json.loads((fw / "firmware.json").read_text())
        m["usb_audio"] = True; m["bluetooth"] = True
        (fw / "firmware.json").write_text(json.dumps(m, indent=2))
        r = subprocess.run(["platform/build_firmware.sh", str(fw), f"{d}/pt.bin"], cwd=ROOT,
                           capture_output=True, text=True)
        if r.returncode: sys.exit(r.stdout + r.stderr + "\ncheck_budget: the build failed")
        return r.stdout


def main():
    args = sys.argv[1:]
    now = measure(pathlib.Path(args[0]).read_text() if args and args[0] != "--show" else build())
    if "--show" in args:
        print(json.dumps(now, indent=2)); return
    budget = json.loads(BUDGET.read_text())
    bad = 0
    for k, (_, name) in WHAT.items():
        over = now[k] > budget[k]
        bad += over
        print(f"{'OVER' if over else 'ok  '} {name}: {now[k]} bytes (1.x limit {budget[k]})")
    if bad:
        sys.exit("The platform takes more than 1.x allows, so a firmware made for 1.0 may no longer fit. Make it "
                 "smaller again; never raise compat/1.0/budget.json (docs/platform/api.md, Versions).")


if __name__ == "__main__":
    main()
