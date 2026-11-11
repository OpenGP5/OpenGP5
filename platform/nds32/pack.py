#!/usr/bin/env python3
"""pack.py: check out/app.bin as a loader-valid section 0x62 and write it out. No device access, no stock image.

  1. checks the image header our start-up code (start/vectors.S) emits at 0xA4..0x105 against
     header_a4_ff.bin (the layout the factory loader reads: const/user data addresses, chip id, magic, ...)
  2. checks what the factory loader checks (check_user_code @0x4CBA): every word in 0x00..0xA3 starts
     with 0x48 (`j`) and lands inside the image, word at 0xC0 == 0xB0BEBDC9; plus size <= 0x168000
  3. writes out/app_0x62.bin (the raw section) and out/gp5-standalone-app.bin (an image holding only it)
"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gp5tool

OUT_APP = os.path.join(HERE, "out", "app_0x62.bin")
OUT_IMG = os.path.join(HERE, "out", "gp5-standalone-app.bin")
HDR0, HDR1 = 0xA4, 0x106


def main():
    app = open(os.path.join(HERE, "out", "app.bin"), "rb").read()
    want = open(os.path.join(HERE, "header_a4_ff.bin"), "rb").read() + b"\xff\xff\xff\x00\xff\xff"
    assert app[HDR0:HDR1] == want[:HDR1 - HDR0], "image header at 0xA4 differs from the loader's layout"
    assert app[0x106:0x108] == bytes.fromhex("dd9e"), "header stub does not end with `ret5 $lp` at 0x106"
    bad = [o for o in range(0, 0xA4, 4) if app[o] != 0x48]
    assert not bad, f"vector words not `j`: {[hex(o) for o in bad]}"
    assert struct.unpack_from("<I", app, 0xC0)[0] == 0xB0BEBDC9
    assert len(app) <= 0x168000
    for o in range(0, 0xA4, 4):                       # every vector must land inside our image
        w = int.from_bytes(app[o:o + 4], "big"); disp = (w & 0xFFFFFF) << 1
        assert w >> 24 == 0x48 and 0x106 <= o + disp < len(app), f"vector {o:#x} target out of image"
    open(OUT_APP, "wb").write(app)
    open(OUT_IMG, "wb").write(gp5tool.make_image({0x62: app}))
    print(f"OK  app {len(app)} bytes ({len(app):#x}), reset -> {0x10000 + ((int.from_bytes(app[0:4], 'big') & 0xFFFFFF) << 1):#x}")
    print(f"    {os.path.relpath(OUT_APP, ROOT)}\n    {os.path.relpath(OUT_IMG, ROOT)}")


if __name__ == "__main__":
    main()
