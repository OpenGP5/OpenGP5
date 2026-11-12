#!/usr/bin/env python3
"""Tiny RGBA PNG reader / writer and helpers, standard library only (no image library on the build machines).
Used to prepare web artwork (tools/web_header.py)."""
import struct, zlib


def read_png(path):
    """-> (w, h, list of [r, g, b, a]) for 8-bit RGB / RGBA PNGs, not interlaced."""
    data = open(path, "rb").read()
    o, idat = 8, b""
    while o < len(data):
        n, t = struct.unpack(">I4s", data[o:o + 8]); d = data[o + 8:o + 8 + n]
        if t == b"IHDR": w, h, depth, ctype, _, _, il = struct.unpack(">IIBBBBB", d)
        elif t == b"IDAT": idat += d
        o += 12 + n
    assert depth == 8 and not il and ctype in (2, 6), (depth, ctype, il)
    ch = 4 if ctype == 6 else 3
    raw, stride, prev, px = zlib.decompress(idat), w * ch, bytearray(w * ch), []
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0; b = prev[i]; c = prev[i - ch] if i >= ch else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            q = line[x * ch:(x + 1) * ch]
            px.append([q[0], q[1], q[2], q[3] if ch == 4 else 255])
    return w, h, px


def write_png(path, w, h, px):
    """RGBA, pixels only (no metadata)."""
    raw = b"".join(b"\0" + bytes(v for p in px[y * w:(y + 1) * w] for v in p) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
