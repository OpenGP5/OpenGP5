#!/usr/bin/env python3
"""Turn any font into a header for the platform's text drawing (platform/nds32/gp5_font.h).

  python3 tools/make_font.py FONT.ttf NAME SIZE_PX OUT.h --notice TEXT [--source TEXT] [--chars TEXT]
  python3 tools/make_font.py FONT.bdf NAME SCALE   OUT.h --notice TEXT [--source TEXT] [--chars TEXT]

A TrueType / OpenType font is rasterised at SIZE_PX pixels per em, smooth (16 levels of coverage). A BDF
bitmap font is copied pixel for pixel, each font pixel drawn SCALE x SCALE screen pixels (1 = native size).
Characters: printable ASCII (0x20..0x7E), or only those in --chars (the others take no room and draw nothing:
useful for big digits). Each glyph keeps only its rows with ink; the line height is ascent + descent.
The header defines `static const gp5_font_t NAME`. Put it in your firmware's nds32/ folder and include it: only
the fonts a firmware includes end up in its image. --notice is the font's copyright / licence line, kept in the
header as font licences ask (keep the licence file next to it too). Needs Pillow for TrueType fonts.
"""
import sys

FIRST, LAST = 0x20, 0x7E


def ttf_glyphs(path, size):
    from PIL import Image, ImageDraw, ImageFont
    f = ImageFont.truetype(path, size)
    asc, desc = f.getmetrics()
    h = asc + desc
    out = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        adv = round(f.getlength(ch))
        bb = f.getbbox(ch, anchor="ls")
        left = min(0, bb[0])
        w = max(adv, bb[2]) - left
        img = Image.new("L", (max(w, 1), h), 0)
        ImageDraw.Draw(img).text((-left, asc), ch, fill=255, font=f, anchor="ls")
        px = img.load()
        cov = [[min(15, (px[x, y] + 8) // 17) for x in range(w)] for y in range(h)]
        out.append((w, adv, left, cov))
    name = " ".join(f.getname())
    return h, asc, out, name


def bdf_glyphs(path, scale):
    lines = open(path, encoding="latin-1").read().splitlines()
    props, glyphs, i = {}, {}, 0
    while i < len(lines):
        t = lines[i].split()
        if t and t[0] in ("FONT_ASCENT", "FONT_DESCENT"): props[t[0]] = int(t[1])
        if t and t[0] == "FAMILY_NAME": props["name"] = lines[i].split(" ", 1)[1].strip('"')
        if t and t[0] == "STARTCHAR":
            g = {}
            i += 1
            while not lines[i].startswith("BITMAP"):
                u = lines[i].split()
                if u[0] == "ENCODING": g["code"] = int(u[1])
                if u[0] == "DWIDTH": g["adv"] = int(u[1])
                if u[0] == "BBX": g["bbx"] = list(map(int, u[1:5]))
                i += 1
            rows = []
            i += 1
            while lines[i] != "ENDCHAR":
                rows.append(int(lines[i], 16)); i += 1
            g["rows"] = rows
            if FIRST <= g.get("code", -1) <= LAST: glyphs[g["code"]] = g
        i += 1
    asc, desc = props["FONT_ASCENT"], props["FONT_DESCENT"]
    h = asc + desc
    out = []
    for code in range(FIRST, LAST + 1):
        g = glyphs[code]
        bw, bh, bx, by = g["bbx"]
        adv = g["adv"]
        left = min(0, bx)
        w = max(adv, bx + bw) - left
        nbytes = (bw + 7) // 8
        cov = [[0] * w for _ in range(h)]
        top = asc - (by + bh)                        # row of the box's top edge, from the top of the line
        for r, bits in enumerate(g["rows"]):
            for c in range(bw):
                if bits >> (8 * nbytes - 1 - c) & 1:
                    y, x = top + r, bx - left + c
                    if 0 <= y < h: cov[y][x] = 15
        out.append((w, adv, left, cov))
    if scale > 1:
        out = [(w * scale, adv * scale, left * scale,
                [[v for v in row for _ in range(scale)] for row in cov for _ in range(scale)])
               for (w, adv, left, cov) in out]
    return h * scale, asc * scale, out, props.get("name", "BDF font")


def main():
    a = sys.argv[1:]
    opts = {}
    for k in ("--notice", "--source", "--chars"):
        if k in a:
            i = a.index(k); opts[k] = a[i + 1]; del a[i:i + 2]
    if len(a) != 4 or "--notice" not in opts: sys.exit(__doc__)
    path, name, size, out = a[0], a[1], int(a[2]), a[3]
    if path.lower().endswith(".bdf"):
        h, base, glyphs, fam = bdf_glyphs(path, size)
        how = "copied pixel for pixel" + (" at %dx" % size if size > 1 else "")
    else:
        h, base, glyphs, fam = ttf_glyphs(path, size)
        how = "rasterised at %d px per em, 16 coverage levels" % size
    keep = opts.get("--chars")
    if keep is not None:                                  # only these characters: the rest take no room
        glyphs = [g if chr(FIRST + i) in keep else (0, 0, 0, []) for i, g in enumerate(glyphs)]
    data, table = bytearray(), []
    for (w, adv, left, cov) in glyphs:
        used = [y for y, row in enumerate(cov) if any(row)]   # crop to the rows that have ink
        top, rows = (used[0], used[-1] - used[0] + 1) if used else (0, 0)
        nib = [v for row in cov[top:top + rows] for v in row]
        if len(nib) % 2: nib.append(0)
        table.append((w, adv, left, top, rows, len(data)))
        data += bytes((nib[i] << 4) | nib[i + 1] for i in range(0, len(nib), 2))
    src = opts.get("--source", fam)
    guard = "FONT_%s_H" % name.upper()
    o = ["/* Generated by tools/make_font.py: do not edit.",
         " * From: %s, %s. Line height %d px, baseline %d px from the top." % (src, how, h, base),
         " * Font licence: %s */" % opts["--notice"],
         "#ifndef %s" % guard, "#define %s" % guard, '#include "gp5_font.h"',
         "static const uint8_t %s_data[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 32):
        o.append("    " + ",".join("0x%02x" % b for b in data[i:i + 32]) + ",")
    o.append("};")
    o.append("static const gp5_glyph_t %s_glyphs[%d] = {" % (name, len(table)))
    for code, (w, adv, left, top, rows, off) in zip(range(FIRST, LAST + 1), table):
        c = chr(code)
        shown = {"\\": "backslash", "*": "star", "/": "slash"}.get(c, c)
        o.append("    { %d, %d, %d, %d, %d, %d },   /* %s */" % (w, adv, left, top, rows, off, shown))
    o.append("};")
    o.append("static const gp5_font_t %s = { %d, %d, %s_glyphs, %s_data };" % (name, h, base, name, name))
    o.append("#endif")
    open(out, "w").write("\n".join(o) + "\n")
    print("%s: %s, line height %d, %d bytes of pixels" % (out, name, h, len(data)))


if __name__ == "__main__":
    main()
