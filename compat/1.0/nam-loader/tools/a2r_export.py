#!/usr/bin/env python3
"""Quantize a NAM A2 Lite (.nam) for the 32-bit-residual kernel a2r.c.

  a2r_export.py model.nam calib48k.wav out.h     a C header (the model compiled in)
  a2r_export.py model.nam calib48k.wav out.a2r   a capture file for the pedal (a2r_blob.h; OpenGP5 Launchpad loads these)
  a2r_export.py model.nam calib48k.wav out.inc   the capture file as a C array (a built-in default capture)
  [--headroom 4] [--name "shown on the screen"]
calib48k.wav may be the word synth: calib_synth.py's made-up signal instead of NAM's input recording.
Formats: see a2r.h. Ranges come from tools/a2q_export.py's float forward over the calibration audio
(the NAM input file at 48 kHz: full scale, so the ranges cover real playing).
"""
import argparse, json, math, struct, sys, zlib
from pathlib import Path
import numpy as np, soundfile as sf
sys.path.insert(0, str(Path(__file__).resolve().parent))
import a2_export, a2q_export as Q

I16, I32 = 32767, 2 ** 31 - 1
PEDAL_INPUT_DBU = 7.1   # the GP-5's input at full scale (measured: a test tone through the pedal and an audio interface)
OUTPUT_LEVEL = -20.0    # every capture's loudness (K-weighted, dB) for the plucked chords below: the middle of 48 captures
OUTPUT_PEAK_MAX = -3.0  # ... unless that puts their peak above this (dBFS)
PLAY_SCALE = 0.17782794100389229   # the chords at -15 dBFS peak: hard playing measured at the pedal's input (2026-11-17)
# BS.1770 K-weighting at 48 kHz (the shelf, then the high-pass)
KW = ((1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585),
      (1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621))


def calibration_gain(nam, pedal_dbu=PEDAL_INPUT_DBU):
    """The input gain that makes the pedal drive a capture as hard as it was recorded: its metadata says what level
    its full scale was (input_level_dbu); the pedal's is pedal_dbu. 1.0 for a capture without that (uncalibrated)."""
    dbu = (nam.get('metadata') or {}).get('input_level_dbu')
    return 10 ** ((pedal_dbu - float(dbu)) / 20) if isinstance(dbu, (int, float)) else 1.0


def output_level(m):
    """The capture's loudness (K-weighted, dB) and peak (dBFS) for the first 3 s of the calibration signal (plucked
    chords) at PLAY_SCALE, the first 50 ms left out. Written out sample by sample: gp5nam.js outputLevel() does the
    very same sums. Returns the gain (dB, to 0.01) that puts it at OUTPUT_LEVEL with its peak at most OUTPUT_PEAK_MAX."""
    import calib_synth                         # always the made-up chords, whatever audio sets the ranges
    y, _ = Q.forward_float(m, np.array(calib_synth.calib_signal()[:3 * 48000]) * PLAY_SCALE)
    y = [float(v) for v in y[2400:]]
    kw = y
    for b0, b1, b2, a1, a2 in KW:
        x1 = x2 = y1 = y2 = 0.0; o = []
        for x in kw:
            v = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1, y2, y1 = x1, x, y1, v; o.append(v)
        kw = o
    ms = 0.0
    for v in kw: ms += v * v
    loud = 10 * math.log10(ms / len(kw) + 1e-30)
    pk = 0.0
    for v in y: pk = max(pk, abs(v))
    peak = 20 * math.log10(pk + 1e-30)
    g = max(-40.0, min(40.0, OUTPUT_LEVEL - loud, OUTPUT_PEAK_MAX - peak))
    return math.floor(g * 100 + 0.5) / 100, loud, peak


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('model'); ap.add_argument('calib'); ap.add_argument('out')
    ap.add_argument('--headroom', type=float, default=4.0)
    ap.add_argument('--no-calibration', action='store_true', help='ignore the capture\'s input_level_dbu')
    ap.add_argument('--no-level', action='store_true', help='keep the capture\'s own output level')
    ap.add_argument('--name', default=None, help='header: C name (a2r_model); capture file: the name shown (default: file name)')
    a = ap.parse_args()
    nam = json.loads(Path(a.model).read_text())
    m = Q.unpack(a2_export.check_a2(a2_export.pick_wavenet(nam, 3)))
    # calibration: the guitar enters only through rech and each layer's mix, so scaling those is the input gain
    g = 1.0 if a.no_calibration else calibration_gain(nam)
    m['rech'] = m['rech'] * g
    for L in m['layers']: L['mix'] = L['mix'] * g
    if g != 1.0: print(f'calibrated: input {20 * np.log10(g):+.1f} dB (capture {nam["metadata"]["input_level_dbu"]} dBu, pedal {PEDAL_INPUT_DBU} dBu)')
    if a.calib == 'synth':                     # the made-up signal (calib_synth.py): what the web page uses
        import calib_synth
        u = np.array(calib_synth.calib_signal())
    else:
        u, sr = sf.read(a.calib, dtype='float64', always_2d=True); u = u.mean(1)
    if not a.no_level:                         # output: every capture equally loud (the head's scale; no range changes)
        gdb, loud, peak = output_level(m)
        m['hs'] = m['hs'] * 10 ** (gdb / 20)
        print(f'output level: {loud:.1f} dB, peak {peak:.1f} dBFS -> {gdb:+.2f} dB')
    _, R = Q.forward_float(m, u)
    hr = a.headroom
    X = Q.exp_for(max(R['x']), I32, hr)
    layers = []
    for l, L in enumerate(m['layers']):
        ez = min(Q.exp_for(R['z'][l], I32, hr), X - 16 + Q.exp_for(np.abs(L['W']).max(), I16))
        while (max(abs(v) for v in L['mix']) * 2 ** (ez + 1) > I32 or max(abs(v) for v in L['b']) * 2 ** ez > 2 ** 30):
            ez -= 1
        ew = ez - X + 16
        # 1x1 output r (the residual update) is bounded by the stream's range
        er = min(Q.exp_for(2 * max(R['x'][l], R['x'][l + 1]), I32, hr), ez - 16 + Q.exp_for(np.abs(L['l1w']).max(), I16))
        while max(abs(v) for v in L['l1b']) * 2 ** er > 2 ** 30:
            er -= 1
        e1 = er - ez + 16
        layers.append(dict(K=L['K'], D=L['D'], ez=ez, er=er, ew=ew, e1=e1,
                           w=[int(round(v * 2 ** ew)) for v in L['W'].ravel()],
                           b=[int(round(v * 2 ** ez)) for v in L['b']],
                           mix=[int(round(v * 2 ** (ez + 1))) for v in L['mix']],
                           l1w=[int(round(v * 2 ** e1)) for v in L['l1w'].ravel()],
                           l1b=[int(round(v * 2 ** er)) for v in L['l1b']]))
        d = layers[-1]
        assert all(abs(v) <= I16 for v in d['w'] + d['l1w']), l
    eh = min(Q.exp_for(R['head'], I32, hr), min(d['ez'] for d in layers))
    s = np.abs(m['hw']).sum() + 1e-12
    ehw = min(Q.exp_for(s * 2 ** 31, 2 ** 62), Q.exp_for(np.abs(m['hw']).max(), I16))
    q = dict(nl=len(layers), kh=m['KH'], X=X, eh=eh, ehw=ehw, slope=int(round(m['slope'] * 65536)),
             rech=[int(round(v * 2 ** (X + 1))) for v in m['rech']], hb=float(m['hb']), hs=float(m['hs']),
             hw=[int(round(v * 2 ** ehw)) for v in m['hw'].ravel()], layers=layers)
    assert all(abs(v) <= I32 for v in q['rech']) and all(abs(v) <= I16 for v in q['hw'])
    name = a.name if a.name is not None else Path(a.model).stem
    out = Path(a.out)
    if out.suffix in ('.a2r', '.inc'):
        blob = make_blob(q, name)
        if out.suffix == '.a2r': out.write_bytes(blob)
        else:
            w = struct.unpack('<%dI' % (len(blob) // 4), blob)
            out.write_text('/* capture "%s" (tools/a2r_export.py), a2r_blob.h format */\n' % name.replace('*/', '')
                           + 'static const uint32_t a2r_default_capture[%d] = {\n' % len(w)
                           + ',\n'.join(', '.join('0x%08x' % v for v in w[i:i + 8]) for i in range(0, len(w), 8)) + '\n};\n')
        print(f'wrote {out}: {len(blob)} bytes, "{name[:24]}"')
    else:
        out.write_text(emit_header(q, a.name or 'a2r_model'))
        print(f'wrote {out}')
    print(f'  X={X} ez={[d["ez"] for d in layers]} ew={[d["ew"] for d in layers]} er={[d["er"] for d in layers]} eh={eh}')


def make_blob(q, name):
    """a2r_blob.h: header, layers, head weights; little-endian; CRC-32 (zlib) over bytes 12.. ."""
    nm = name.encode('utf-8', 'replace')[:24].ljust(24, b'\0')
    body = struct.pack('<BBbbb3xi3iff24s', q['nl'], q['kh'], q['X'], q['eh'], q['ehw'], q['slope'], *q['rech'],
                       q['hb'], q['hs'], nm)
    for d in q['layers']:
        body += struct.pack('<BxHbb2x3i3i3i9h2x', d['K'], d['D'], d['ez'], d['er'], *d['b'], *d['mix'], *d['l1b'], *d['l1w'])
        w = struct.pack('<%dh' % len(d['w']), *d['w'])
        body += w + b'\0' * (-len(w) % 4)
    hw = struct.pack('<%dh' % len(q['hw']), *q['hw'])
    body += hw + b'\0' * (-len(hw) % 4)
    length = 12 + len(body)
    return struct.pack('<4sII', b'A2R1', length, zlib.crc32(body) & 0xFFFFFFFF) + body


def emit_header(q, n):
    layers = q['layers']
    o = ['/* Generated by tools/a2r_export.py: NAM A2 Lite for the 32-bit-residual kernel a2r.c. */',
         '#include "a2r.h"']
    for l, d in enumerate(layers):
        o.append(f'static const int16_t {n}_w{l}[] = {{ {", ".join(map(str, d["w"]))} }};')
    o.append(f'static const int16_t {n}_hw[] = {{ {", ".join(str(v) for v in q["hw"])} }};')
    o.append(f'static const a2r_layer_def_t {n}_layers[] = {{')
    for l, d in enumerate(layers):
        o.append(f'  {{ {d["K"]}, {d["D"]}, {d["ez"]}, {d["er"]}, {n}_w{l}, {{ {", ".join(map(str, d["b"]))} }}, '
                 f'{{ {", ".join(map(str, d["mix"]))} }}, {{ {", ".join(map(str, d["l1w"]))} }}, '
                 f'{{ {", ".join(map(str, d["l1b"]))} }} }},')
    o.append('};')
    o.append(f'static const a2r_model_t {n} = {{ {len(layers)}, {q["kh"]}, {q["slope"]}, {q["X"]}, '
             f'{{ {", ".join(map(str, q["rech"]))} }}, {q["eh"]}, {q["ehw"]}, {n}_hw, {q["hb"]!r}f, {q["hs"]!r}f, '
             f'{n}_layers }};')
    return '\n'.join(o) + '\n'


if __name__ == '__main__':
    main()
