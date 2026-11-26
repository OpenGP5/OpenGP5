#!/usr/bin/env python3
"""A cab IR (.wav) -> the NAM player's storage file "cab" (OpenGP5 Launchpad does the same in its Tones page).

  make_cab.py ir.wav out.cab [--taps 1024]

Mono (channels averaged), resampled to 48 kHz if needed, cut to --taps (fade over the last 5 %), scaled to the same
loudness as every other IR (LOUDNESS_DB, weighted roughly as a guitar is heard), its loudest frequency at most PEAK_MAX_DB. File: "CAB1", taps, 48000, 0 (u32 LE), then taps x float32 LE."""
import argparse, struct
from math import gcd
import numpy as np, soundfile as sf

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('wav'); ap.add_argument('out'); ap.add_argument('--taps', type=int, default=1024)
a = ap.parse_args()

LOUDNESS_DB = -6.0   # the middle of 51 IRs from five packs when only their peak was set (they spread over 3.4 dB)
PEAK_MAX_DB = 2.0    # unit energy put the Orange 4x12 at +13 dB at 129 Hz and the pedal clipped; this stays far below

def weight(f):
    """A rough hearing weight for a guitar through a cab: K-weighting (BS.1770 shelf + high-pass, analog form), a pink
    spectrum (1/f), the amp's top rolled off above 3 kHz; 60 Hz .. 12 kHz only. Same formula in Launchpad's cabir.js."""
    f = np.asarray(f, dtype=np.float64); w = np.zeros_like(f); m = (f >= 60) & (f <= 12000); f = f[m]
    x, y, a2 = f / 1500, f / 38, 10 ** (4 / 10)
    w[m] = (1 / f) * (1 + a2 * x * x) / (1 + x * x) * y ** 4 / (1 + y * y) ** 2 / (1 + (f / 3000) ** 4)
    return w

def cab_gain(h):
    """loudest frequency at 0 dB, then to LOUDNESS_DB, but its loudest frequency no higher than PEAK_MAX_DB"""
    g = 1 / (np.abs(np.fft.rfft(h, 8192)).max() + 1e-12)
    w = weight(np.arange(8193) * 48000 / 16384)
    loud = 10 * np.log10(np.sum(np.abs(np.fft.rfft(h * g, 16384)) ** 2 * w) / np.sum(w) + 1e-30)
    return g * min(10 ** ((LOUDNESS_DB - loud) / 20), 10 ** (PEAK_MAX_DB / 20))
h, sr = sf.read(a.wav, dtype='float64', always_2d=True)
h = h.mean(1)
if sr != 48000:
    from scipy.signal import resample_poly
    g = gcd(48000, int(sr)); h = resample_poly(h, 48000 // g, int(sr) // g)
n = min(a.taps, len(h)); h = h[:n].copy()
f = max(1, n // 20); h[n - f:] *= np.linspace(1, 0, f)
h *= cab_gain(h)
open(a.out, 'wb').write(struct.pack('<4sIII', b'CAB1', n, 48000, 0) + h.astype('<f4').tobytes())
print(f'{a.out}: {n} taps at 48 kHz (from {len(h)} at {sr} Hz), {16 + 4 * n} bytes')
