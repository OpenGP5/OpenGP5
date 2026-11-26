#!/usr/bin/env python3
"""Quantize a NAM A2 Lite (.nam) for a 16-bit kernel and emit a C header. a2r_export.py uses only its float forward.

Number format (all power-of-two scales, value = q * 2^-e):
  u (input)            int16, e=15 (engine float in [-1,1))
  x_l (layer input)    int16 in the layer's ring, e = ex[l]   (calibrated: max|x_l| * HEADROOM)
  conv/mix/bias acc    int32, e = ez[l] = ew[l] + ex[l]; ew chosen so the accumulator CANNOT
                       overflow for any int16 history (sum|W|*2^15 bound) — no calibration risk
  a_l (post-LeakyReLU) int16 for the 1x1, e = ea[l] (calibrated); head sum int32, e = eh
  1x1 acc              int32, e = e1[l] + ea[l] (overflow-safe by the same bound)
  head conv            int32 ring x int16 weights -> int64 acc, then float * head_scale
Calibration runs a float64 numpy forward (identical math to a2_lite.c) over --calib audio,
which should be full-scale (the NAM input wav peaks at 1.0) so ranges cover the worst case.

  a2q_export.py model.nam calib48k.wav out.h [--width 3] [--headroom 2]
Needs numpy + soundfile.
"""
import argparse, json, math, sys
from pathlib import Path
import numpy as np
import soundfile as sf
sys.path.insert(0, str(Path(__file__).resolve().parent))
import a2_export

I16, I32 = 32767, 2 ** 31 - 1


def unpack(a):
    C, w = a['C'], np.asarray(a['w'], np.float64)
    p = 0
    def take(n, shape):
        nonlocal p
        v = w[p:p + n].reshape(shape); p += n; return v
    m = dict(C=C, rech=take(C, (C,)), layers=[])
    for K, D in zip(a['K'], a['D']):
        m['layers'].append(dict(K=K, D=D, W=take(C * C * K, (C, C, K)), b=take(C, (C,)), mix=take(C, (C,)),
                                l1w=take(C * C, (C, C)), l1b=take(C, (C,))))
    m['hw'] = take(C * a['KH'], (C, a['KH'])); m['hb'] = take(1, (1,))[0]; m['hs'] = take(1, (1,))[0]
    assert p == len(w)
    m['slope'], m['KH'] = a['slope'], a['KH']
    return m


def forward_float(m, u):
    """float64 layer-major forward == a2_lite.c with prime (NAM pad_start). Returns y, ranges."""
    rf = m['KH'] + sum((L['K'] - 1) * L['D'] for L in m['layers'])
    up = np.concatenate([np.zeros(rf - 1), u.astype(np.float64)])
    def delay(v, s):
        return v if s == 0 else np.concatenate([np.zeros((v.shape[0], s)), v[:, :-s]], 1)
    x = m['rech'][:, None] * up[None]
    head = np.zeros_like(x)
    R = dict(x=[], z=[], a=[])
    for L in m['layers']:
        R['x'].append(np.abs(x).max())
        z = L['b'][:, None] + L['mix'][:, None] * up[None]
        for k in range(L['K']):
            z += L['W'][:, :, k] @ delay(x, (L['K'] - 1 - k) * L['D'])
        a = np.where(z > 0, z, m['slope'] * z)
        R['z'].append(np.abs(z).max()); R['a'].append(np.abs(a).max())
        head += a
        x = x + L['l1w'] @ a + L['l1b'][:, None]
    R['x'].append(np.abs(x).max())
    R['head'] = np.abs(head).max()
    y = m['hb'] + sum(m['hw'][:, k] @ delay(head, m['KH'] - 1 - k) for k in range(m['KH']))
    return (m['hs'] * y)[rf - 1:], R


def exp_for(maxabs, lim, headroom=1.0, cap=30):
    if maxabs <= 0:
        return cap
    return min(cap, int(math.floor(math.log2(lim / (maxabs * headroom)))))


def quantize(m, R, headroom):
    C = m['C']
    q = dict(C=C, KH=m['KH'], slope_q16=int(round(m['slope'] * 65536)), layers=[])
    ex = [exp_for(v, I16, headroom) for v in R['x']]
    for l in range(1, len(ex)):          # kernel only right-shifts: exponents non-increasing
        ex[l] = min(ex[l], ex[l - 1])
    q['ex0'] = ex[0]
    q['rech'] = [int(round(v * 2 ** ex[0])) for v in m['rech']]   # x0 = (rech_q * u_q) >> 15
    eh = exp_for(R['head'], I32, 4 * headroom)
    q['eh'] = eh
    for l, L in enumerate(m['layers']):
        # overflow-safe conv weight exponent: (sum|W_o| + |mix_o|) * 2^ew * 2^15 + |b|*2^ez < 2^30
        s = max(np.abs(L['W'][o]).sum() + abs(L['mix'][o]) for o in range(C)) + 1e-12
        ew = min(exp_for(s * 2 ** 15, 2 ** 30), exp_for(np.abs(L['W']).max(), I16))
        ez = ew + ex[l]
        while max(abs(v) for v in L['b']) * 2 ** ez > 2 ** 29:
            ew -= 1; ez -= 1
        ea = min(exp_for(R['a'][l], I16, headroom), ez)
        s1 = max(np.abs(L['l1w'][o]).sum() for o in range(C)) + 1e-12
        e1 = min(exp_for(s1 * 2 ** 15, 2 ** 30), exp_for(np.abs(L['l1w']).max(), I16))
        while max(abs(v) for v in L['l1b']) * 2 ** (e1 + ea) > 2 ** 29:
            e1 -= 1
        assert e1 + ea >= ex[l + 1], 'kernel needs e1a >= ex_out'
        q['layers'].append(dict(
            K=L['K'], D=L['D'], ex_in=ex[l], ex_out=ex[l + 1], ez=ez, ea=ea, e1a=e1 + ea,
            w=[int(round(v * 2 ** ew)) for v in L['W'].ravel()],          # [o][i][k]
            b=[int(round(v * 2 ** ez)) for v in L['b']],
            mix=[int(round(v * 2 ** (ez + 1))) for v in L['mix']],      # SMLAWB: (mix * u_q) >> 16
            l1w=[int(round(v * 2 ** e1)) for v in L['l1w'].ravel()],
            l1b=[int(round(v * 2 ** (e1 + ea))) for v in L['l1b']]))
        assert all(abs(v) <= I16 for v in q['layers'][-1]['w'] + q['layers'][-1]['l1w'])
        assert all(abs(v) <= I32 for v in q['layers'][-1]['mix'])
    q['eh'] = min(q['eh'], min(d['ez'] for d in q['layers']))   # head add is a right shift
    s = np.abs(m['hw']).sum() + 1e-12
    q['ehw'] = min(exp_for(s * 2 ** 31, 2 ** 62), exp_for(np.abs(m['hw']).max(), I16))
    q['hw'] = [int(round(v * 2 ** q['ehw'])) for v in m['hw'].ravel()]   # [c][k]
    q['hb'], q['hs'] = float(m['hb']), float(m['hs'])
    q['ring'] = sum((L['K'] - 1) * L['D'] + 1 for L in m['layers']) * C
    return q


def emit(q, name='a2q_model'):
    L = q['layers']; C = q['C']
    o = ['/* Generated by tools/a2q_export.py — NAM A2 Lite quantized for a2_lite_q15. */',
         '#include "a2_lite_q15.h"',
         f'#define A2Q_MODEL_RING {q["ring"]}  /* int16 history values (reference layout) */']
    for l, d in enumerate(L):
        o.append(f'static const int16_t {name}_w{l}[] = {{ {", ".join(map(str, d["w"]))} }};')
    o.append(f'static const int16_t {name}_hw[] = {{ {", ".join(map(str, q["hw"]))} }};')
    o.append(f'static const a2q_layer_def_t {name}_layers[] = {{')
    for l, d in enumerate(L):
        o.append(f'  {{ {d["K"]}, {d["D"]}, {d["ex_in"]}, {d["ex_out"]}, {d["ez"]}, {d["ea"]}, {d["e1a"]}, '
                 f'{name}_w{l}, {{ {", ".join(map(str, d["b"]))} }}, {{ {", ".join(map(str, d["mix"]))} }}, '
                 f'{{ {", ".join(map(str, d["l1w"]))} }}, {{ {", ".join(map(str, d["l1b"]))} }} }},')
    o.append('};')
    o.append(f'static const a2q_model_t {name} = {{ {len(L)}, {q["KH"]}, {q["slope_q16"]}, {q["ex0"]}, '
             f'{{ {", ".join(map(str, q["rech"]))} }}, {q["eh"]}, {q["ehw"]}, {name}_hw, '
             f'{q["hb"]!r}f, {q["hs"]!r}f, {name}_layers }};')
    return '\n'.join(o) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('model'); ap.add_argument('calib'); ap.add_argument('out')
    ap.add_argument('--width', type=int, default=3)
    ap.add_argument('--headroom', type=float, default=2.0)
    a = ap.parse_args()
    m = unpack(a2_export.check_a2(a2_export.pick_wavenet(json.loads(Path(a.model).read_text()), a.width)))
    if m['C'] != 3:
        sys.exit('a2_lite_q15 is specialised for C=3 (A2 Lite)')
    u, sr = sf.read(a.calib, dtype='float64', always_2d=True); assert sr == 48000
    _, R = forward_float(m, u.mean(1))
    q = quantize(m, R, a.headroom)
    Path(a.out).write_text(emit(q))
    print(f'wrote {a.out}: ex={[d["ex_in"] for d in q["layers"]]} ez={[d["ez"] for d in q["layers"]]} '
          f'eh={q["eh"]} ring={q["ring"]} int16 ({q["ring"]*2/1024:.1f} KB)')


if __name__ == '__main__':
    main()
