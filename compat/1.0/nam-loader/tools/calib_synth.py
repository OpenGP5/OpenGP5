#!/usr/bin/env python3
"""A made-up calibration signal for a2r_export.py, instead of NAM's input recording.

The exporter only needs audio that drives a capture as hard as real playing will, to measure each layer's range.
This one is 11 s at 48 kHz: plucked chords (Karplus-Strong), a full-scale sine sweep, and noise at two levels.
Only + - * / on doubles and 32-bit integers, so the JavaScript copy (web: gp5nam.js calibSignal) gives the very
same samples.

  calib_synth.py out.wav     write it (to listen to)
"""
import sys

SR = 48000


def lcg(state):
    state[0] = (1664525 * state[0] + 1013904223) & 0xFFFFFFFF
    return state[0] / 4294967296.0 - 0.5            # -0.5 .. 0.5


def pluck(out, start, period, length, amp, st):
    """one Karplus-Strong string: a noise burst through a delay line with a two-point average"""
    buf = [lcg(st) for _ in range(period)]
    i = 0
    for n in range(length):
        if start + n >= len(out): break
        a = buf[i]; b = buf[(i + 1) % period]
        out[start + n] += amp * a
        buf[i] = 0.4985 * (a + b)
        i = (i + 1) % period


def calib_signal():
    st = [12345]
    n_pl, n_sw, n_no = 6 * SR, 3 * SR, 2 * SR
    pl = [0.0] * n_pl
    chords = [(218, 163, 109), (146, 98, 73), (436, 327, 245), (97, 65, 49), (290, 218, 184), (122, 91, 61),
              (183, 137, 92), (82, 61, 41), (326, 244, 163), (109, 82, 55), (245, 184, 123), (73, 55, 41)]
    for k, ch in enumerate(chords):                  # a chord every half second, the strings 15 ms apart
        for j, p in enumerate(ch):
            pluck(pl, k * SR // 2 + j * 720, p, SR, 1.0, st)
    peak = max(abs(v) for v in pl)
    sig = [v / peak * 0.99 for v in pl]
    x, y = 1.0, 0.0                                  # sweep: "magic circle" oscillator, e from ~20 Hz to ~8 kHz
    e = 0.0026                                       # (its amplitude grows to 1/sqrt(1 - e^2/4): 1.155 at the end)
    g = float.fromhex("0x1.0002b58041b9bp+0")        # (1 / 0.0026) ** (1 / n_sw), written out: no pow() to differ
    for _ in range(n_sw):
        x += e * y; y -= e * x
        sig.append(0.85 * x)
        e *= g
    for n in range(n_no):                            # noise, near full scale then 20 dB down
        v = lcg(st) + lcg(st) + lcg(st) + lcg(st)    # -2 .. 2, roughly Gaussian
        sig.append(v * (0.45 if n < SR else 0.045))
    return sig


if __name__ == "__main__":
    import numpy as np, soundfile as sf
    s = np.array(calib_signal())
    print("samples %d, peak %.3f" % (len(s), np.abs(s).max()))
    if len(sys.argv) > 1: sf.write(sys.argv[1], s, SR, subtype="FLOAT")
