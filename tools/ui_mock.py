#!/usr/bin/env python3
"""
Render mock-ups of the pedal's effect screen on the host.

  ui_mock.py <out.png>

Builds firmwares/granular-delay/nds32/test/ui_mock.c (the real ui.c + fx_ui.c) and lays the 160x80 screens out on
one sheet, 3x enlarged, with their names. No device access.
"""
import struct, subprocess, sys, tempfile, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "firmwares/granular-delay/nds32"
PLAT = ROOT / "platform/nds32"
W, H, K, PAD, COLS = 160, 80, 3, 12, 2

def png(path, w, h, rgb):
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def main():
    out = sys.argv[1]
    with tempfile.TemporaryDirectory() as d:
        exe = Path(d) / "mock"
        subprocess.run(["cc", "-O1", "-Wall", "-Wextra", "-fsanitize=address,undefined", f"-I{PLAT}", str(APP / "test/ui_mock.c"),
                        str(APP / "ui.c"), str(APP / "fx_ui.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe), d], check=True)
        shots = sorted(Path(d).glob("*.rgb"))
        rows = (len(shots) + COLS - 1) // COLS
        sw, sh = COLS * (W * K + PAD) + PAD, rows * (H * K + PAD) + PAD
        sheet = bytearray(bytes([40, 44, 52]) * (sw * sh))
        for n, f in enumerate(shots):
            img = f.read_bytes()
            ox, oy = PAD + (n % COLS) * (W * K + PAD), PAD + (n // COLS) * (H * K + PAD)
            for y in range(H):
                line = b"".join(img[(y * W + x) * 3:(y * W + x) * 3 + 3] * K for x in range(W))
                for k in range(K):
                    o = ((oy + y * K + k) * sw + ox) * 3
                    sheet[o:o + len(line)] = line
        png(out, sw, sh, bytes(sheet))
        print(out, f"{sw}x{sh}", [f.stem for f in shots])

if __name__ == "__main__":
    main()
