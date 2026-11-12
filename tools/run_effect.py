#!/usr/bin/env python3
"""Run a firmware's audio code on the computer: a WAV file in, a stereo WAV file out. No pedal needed.

  python3 tools/run_effect.py <firmware> in.wav out.wav [--set ID=VALUE ...] [--cmd HEX ...] [--tone]

<firmware> is a folder under firmwares/, or the path of a firmware folder elsewhere. The DSP half (dsp/firmware.h) is compiled for the computer with the
platform's own maths functions, so it computes what the pedal computes (the pedal adds the volume knob).
  --set ID=VALUE   a setting, sent as the control half would: link command 0x7F10 [id][value]
                   (VALUE is an integer; for a float setting write its bits, e.g. 0x3f000000 = 0.5)
  --cmd HEX        any link payload to fw_command(), e.g. 117f0400
  --tone           no input file needed: in.wav is ignored and 4 s of a plucked 110 Hz tone is used
in.wav: 16-bit or 24-bit PCM; the first channel is the guitar. 44.1 kHz is what the pedal runs at.
Needs a C compiler (cc).
"""
import argparse, array, math, pathlib, struct, subprocess, sys, tempfile, wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATE = 44100


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width not in (2, 3): sys.exit("in.wav must be 16-bit or 24-bit PCM")
    if rate != RATE: print(f"note: {path} is {rate} Hz; the pedal runs at {RATE} Hz (times will be off)", file=sys.stderr)
    out = array.array("i")
    step = ch * width
    for i in range(0, len(raw), step):
        b = raw[i:i + width]
        v = int.from_bytes(b, "little", signed=True)
        out.append(v << (32 - 8 * width))
    return out


def tone():
    out = array.array("i")
    for i in range(4 * RATE):
        t = i / RATE
        env = math.exp(-1.5 * (t % 2.0))
        v = env * (0.5 * math.sin(2 * math.pi * 110 * t) + 0.25 * math.sin(2 * math.pi * 220 * t))
        out.append(int(v * 0.8 * 2 ** 31))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware"); ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--set", action="append", default=[], metavar="ID=VALUE")
    ap.add_argument("--cmd", action="append", default=[], metavar="HEX")
    ap.add_argument("--tone", action="store_true")
    a = ap.parse_args()
    fw = pathlib.Path(a.firmware).resolve() / "dsp"
    if not (fw / "firmware.h").is_file(): fw = ROOT / "firmwares" / a.firmware / "dsp"
    if not (fw / "firmware.h").is_file(): sys.exit(f"no {fw}/firmware.h")
    cmds = []
    for s in a.set:
        k, _, v = s.partition("=")
        cmds.append(struct.pack("<HHIi" if not v.lower().startswith("0x") else "<HHII", 0x7F10, 12, int(k, 0), int(v, 0)).hex())
    cmds += [c.replace(" ", "") for c in a.cmd]
    samples = tone() if a.tone else read_wav(a.inp)
    with tempfile.TemporaryDirectory() as d:
        exe = pathlib.Path(d) / "run_effect"
        subprocess.run(["cc", "-O2", "-std=c99", "-Wall", "-fno-builtin", f"-I{fw}", f"-I{ROOT/'platform/dsp'}",
                        str(ROOT / "platform/host/run_effect.c"), str(ROOT / "platform/dsp/gp5_math.c"),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe), *cmds], input=samples.tobytes(), capture_output=True)
        sys.stderr.write(r.stderr.decode())
        if r.returncode: sys.exit(r.returncode)
    pcm = array.array("i"); pcm.frombytes(r.stdout)
    clipped = sum(1 for v in pcm if v in (2147483647, -2147483648))
    with wave.open(a.out, "wb") as w:
        w.setnchannels(2); w.setsampwidth(3); w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<i", v)[1:] for v in pcm))
    peak = max((abs(v) for v in pcm), default=0) / 2 ** 31
    print(f"{a.out}: {len(pcm) // 2} frames, peak {20 * math.log10(peak) if peak else -999:.1f} dBFS"
          + (f", {clipped} samples at full scale" if clipped else ""))


if __name__ == "__main__":
    main()
