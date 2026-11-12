#!/usr/bin/env python3
"""Make the web flasher's header from the banner: background removed, pedal scaled to the text's height.

  python3 tools/web_header.py <banner.png> web/flasher/assets/opengp5-header.png

The banner's background (near-black with dark-teal contour lines) and the pedal's dark body become
transparent; light artwork (white wordmark, teal, the pedal's outline) is kept with soft edges. The pedal
is scaled so it spans from the top of the wordmark to the bottom of the subtitle, then everything is
placed on a transparent canvas trimmed to the artwork. Regions are measured from the banner (2000x801).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import img_rgba as I

PEDAL = (63, 73, 367, 724)          # x0, y0, x1, y1 inclusive
TEXT = (424, 167, 1904, 633)
L0, L1 = 45.0, 150.0                # luminance: at or below L0 = background, at or above L1 = solid artwork


def key(p):
    lum = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]
    a = max(0.0, min(1.0, (lum - L0) / (L1 - L0))) * (p[3] / 255)
    if a <= 0: return (0.0, 0.0, 0.0, 0.0)
    return (p[0] / 255 * a, p[1] / 255 * a, p[2] / 255 * a, a)      # premultiplied


def crop(w, px, box, solid_inside=False):
    """Keyed crop. solid_inside: everything enclosed by the artwork's outline stays opaque (the pedal's body);
    only the background reachable from the crop's edge becomes transparent."""
    x0, y0, x1, y1 = box
    cw, ch = x1 - x0 + 1, y1 - y0 + 1
    src = [px[y * w + x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    out = [key(p) for p in src]
    if solid_inside:
        outside = bytearray(cw * ch)
        todo = [i for i in range(cw * ch) if (i % cw in (0, cw - 1) or i // cw in (0, ch - 1)) and out[i][3] < 0.5]
        for i in todo: outside[i] = 1
        while todo:
            i = todo.pop(); x, y = i % cw, i // cw
            for j in ((i - 1) if x else -1, (i + 1) if x < cw - 1 else -1, (i - cw) if y else -1, (i + cw) if y < ch - 1 else -1):
                if j >= 0 and not outside[j] and out[j][3] < 0.5: outside[j] = 1; todo.append(j)
        for i in range(cw * ch):
            if not outside[i]:
                p = src[i]; out[i] = (p[0] / 255, p[1] / 255, p[2] / 255, 1.0)
    return cw, ch, out


def scale(w, h, px, nw, nh):
    """Area-average downscale (premultiplied)."""
    out = []
    for ny in range(nh):
        sy0, sy1 = ny * h / nh, (ny + 1) * h / nh
        for nx in range(nw):
            sx0, sx1 = nx * w / nw, (nx + 1) * w / nw
            acc = [0.0] * 4; tot = 0.0
            for y in range(int(sy0), min(h, int(sy1 + 0.9999))):
                wy = min(sy1, y + 1) - max(sy0, y)
                for x in range(int(sx0), min(w, int(sx1 + 0.9999))):
                    wt = wy * (min(sx1, x + 1) - max(sx0, x))
                    q = px[y * w + x]
                    for k in range(4): acc[k] += q[k] * wt
                    tot += wt
            out.append(tuple(v / tot for v in acc))
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    w, h, px = I.read_png(src)
    assert (w, h) == (2000, 801), "measured for the 2000x801 banner"
    tw, th, text = crop(w, px, TEXT)
    pw, ph, pedal = crop(w, px, PEDAL, solid_inside=True)
    nph = th; npw = round(pw * nph / ph)
    pedal = scale(pw, ph, pedal, npw, nph)
    gap = round((TEXT[0] - PEDAL[2]) * 0.9)
    pad = 4
    W, H = pad + npw + gap + tw + pad, pad + th + pad
    canvas = [(0.0, 0.0, 0.0, 0.0)] * (W * H)
    for y in range(nph):
        for x in range(npw): canvas[(pad + y) * W + pad + x] = pedal[y * npw + x]
    for y in range(th):
        for x in range(tw): canvas[(pad + y) * W + pad + npw + gap + x] = text[y * tw + x]
    out = []
    for r, g, b, a in canvas:                                   # un-premultiply to 8-bit RGBA
        if a <= 0.004: out.append((0, 0, 0, 0)); continue
        out.append((min(255, round(r / a * 255)), min(255, round(g / a * 255)), min(255, round(b / a * 255)), round(a * 255)))
    I.write_png(dst, W, H, out)
    print(f"{dst}: {W}x{H} (pedal {npw}x{nph}, was {pw}x{ph})")


if __name__ == "__main__":
    main()
