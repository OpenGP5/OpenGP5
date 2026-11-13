# Text and fonts

The screen is 160 × 80 pixels. A firmware can write text two ways.

## 1. The built-in pixel font: `gp5_text`

```c
gp5_text(2, "rate 5.00 Hz");          /* line 0..4, up to 13 characters, white on black */
```

Five lines of 13 characters in a 5×7 pixel font drawn at double size, all printable ASCII, lowercase included.
Quick, and nothing to set up: right for status lines, settings and debugging. The font is petabyt's 5×7 font
(https://github.com/petabyt/font, MIT licence), in `platform/nds32/font5x7.h`.

## 2. Smooth text in any font: `gp5_text_at` and `gp5_draw_text`

```c
#include "gp5_font.h"
#include "fonts/sans_24.h"

gp5_text_at(4, 10, &gp5_font_sans_24, "Delay", 0xFFFFFF, 0x101820);   /* straight to the screen */
```

- `gp5_text_at(x, y, font, text, colour, background)` draws one line onto the screen, `x, y` being the top
  left. Colours are `0xRRGGBB`.
- `gp5_draw_text(buffer, w, h, x, y, font, text, colour)` draws into a pixel buffer of your own (the screen's
  format, as `gp5_blit` takes it), blended into what is already there, so text sits on top of your graphics. Then
  send the buffer with `gp5_blit`. The tuner (`firmwares/tuner/nds32/firmware.c`) draws this way.
- `gp5_text_width(font, text)` measures, for centring or right-aligning.
- A font knows its line height (`font->h`) and where its baseline is (`font->base`, pixels below the top).

The platform includes one smooth font, **sans** (derived from Varela, which reads well on small screens), in
six sizes: `fonts/sans_12.h`, `sans_16`, `sans_20`, `sans_24`, `sans_32`, `sans_40`. The number is the size in
pixels; the line is about a third taller. Only the sizes a firmware includes take room in its image (from
about 3 KB for size 12 to about 27 KB for size 40).

## 3. Your own font

Any TrueType or OpenType font (`.ttf`, `.otf`), or any BDF pixel font (`.bdf`), becomes a header for your
firmware with one command. No change to the platform is needed:

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
- `--notice` goes into the header: keep the font's copyright and licence there, and put the font's licence file
  next to the header in your firmware's folder. Most free fonts (SIL Open Font License) ask for exactly that.
  If the licence reserves the font's name (the OFL's "Reserved Font Name"), give your converted copy another
  name, as the platform does with Varela ("sans").
- Make as many sizes as you need; each is its own header.

To check how text looks before flashing, the tuner's screen preview (`firmwares/tuner/test/screen.sh`) shows
how to draw a firmware's screen into PNG files on the computer with the same code that runs on the pedal.
