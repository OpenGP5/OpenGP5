# Text and fonts

The screen is 160 × 80 pixels. Use the built-in pixel font or draw smooth text with a font header.
You can also convert your own font to a header.

## 1. The built-in pixel font: `gp5_text`

```c
gp5_text(2, "rate 5.00 Hz");          /* line 0..4, up to 13 characters, white on black */
```

`gp5_text` draws five lines of up to 13 characters. It supports printable ASCII, including lowercase.
The 5×7 font draws at double size and needs no setup. Use it for settings, status or debugging.
The source is [petabyt's font](https://github.com/petabyt/font), under the MIT licence, in `platform/nds32/font5x7.h`.

## 2. Smooth text in any font: `gp5_text_at` and `gp5_draw_text`

```c
#include "gp5_font.h"
#include "fonts/sans_24.h"

gp5_text_at(4, 10, &gp5_font_sans_24, "Delay", 0xFFFFFF, 0x101820);   /* straight to the screen */
```

- `gp5_text_at(x, y, font, text, colour, background)` draws a line on the screen.
  `x, y` is the top-left corner. Colours use `0xRRGGBB`.
- `gp5_draw_text(buffer, w, h, x, y, font, text, colour)` draws into your own pixel buffer.
  It blends text over the existing pixels. Use the screen format that `gp5_blit` accepts, then send the buffer with that call.
  The tremolo draws this way, one row at a time (`firmwares/tremolo/nds32/firmware.c`).
- `gp5_text_width(font, text)` measures, for centring or right-aligning.
- A font knows its line height (`font->h`) and where its baseline is (`font->base`, pixels below the top).

The platform includes **sans**, based on [Atkinson Hyperlegible Next](https://github.com/googlefonts/atkinson-hyperlegible-next).
It uses weight 600 (semibold). Choose a size from `fonts/sans_12.h`, `sans_16`, `sans_20`, `sans_24`, `sans_32` or `sans_40`.
The number is the pixel size; line height is about a third larger. A build includes the sizes you use.
Size 12 takes about 3 KB; size 40 takes about 26 KB.

The source font supports weights from 200 (extra light) to 800 (extra bold).
Its file is `platform/nds32/fonts/AtkinsonHyperlegibleNext-VariableFont_wght.ttf`.
For a bold title, run:

```
python3 tools/make_font.py platform/nds32/fonts/AtkinsonHyperlegibleNext-VariableFont_wght.ttf font_title 24 \
        firmwares/my-effect/nds32/font_title.h --weight 800 \
        --notice "Atkinson Hyperlegible Next, SIL Open Font License 1.1 (LICENSE-AtkinsonHyperlegibleNext-OFL.txt)"
```

## 3. Your own font

Use `make_font.py` to turn a TrueType (`.ttf`), OpenType (`.otf`) or BDF (`.bdf`) font into a header.
This does not need a platform change:

```
pip install pillow
python3 tools/make_font.py MyFont.ttf font_title 28 firmwares/my-effect/nds32/font_title.h \
        --notice "MyFont, Copyright 20xx The MyFont Authors, SIL Open Font License 1.1"
```

```c
#include "font_title.h"
gp5_text_at(4, 4, &font_title, "My effect", 0xFFFFFF, 0x000000);
```

- `28` is the size in pixels for a TrueType font. For a BDF font it is the scale instead: `1` copies it pixel for
  pixel, `2` doubles every pixel (crisp, for pixel-art looks).
- `--chars "0123456789.-+%"` keeps only the characters you list; the others take no room. A big readout font
  then costs a fraction of a full one.
- `--weight W` picks a variable font's weight. The tool rejects values outside that font's range.
- `--notice` adds a notice to the header. See the licence notes below.
- Make as many sizes as you need; each is its own header.

## Font licences

Keep the font's copyright and licence notice in the header. Put its licence file beside the header.
Read the font's own terms for conversion and sharing. For a font under the SIL Open Font License (OFL),
check whether it lists a Reserved Font Name. A modified font may need a new name.

## Preview text

Use `firmwares/tremolo/test/screen.sh` as a screen-preview example. It runs the same drawing code on your computer
and saves PNG files so you can check the text before flashing.
