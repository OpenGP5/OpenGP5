/* GP-5 open platform: smooth (anti-aliased) text on the screen (platform/nds32/text.c).
 *
 * A font is a C header made by tools/make_font.py from any TrueType / OpenType font, or from a BDF pixel font.
 * Use your own: convert it, put the header in your firmware's nds32/ folder and #include it; only the fonts a
 * firmware includes take room in its image. Or use the platform's own, "sans" (derived from Varela, SIL Open
 * Font License: fonts/LICENSE-Varela-OFL.txt) in sizes 12, 16, 20, 24, 32 and 40 px:
 *     #include "fonts/sans_16.h"      ...      gp5_text_at(4, 20, &gp5_font_sans_16, "Hello", 0xFFFFFF, 0x000000);
 * Fonts cover printable ASCII (space .. ~); other characters draw as '?'. Pixels are coverage (0..15), blended
 * into what is behind them. (The built-in 5x7 pixel font of gp5_text() is separate: gp5_firmware.h.) */
#ifndef GP5_FONT_H
#define GP5_FONT_H
#include <stdint.h>

typedef struct {
    uint8_t w;          /* box width in pixels */
    uint8_t adv;        /* how far the next character starts */
    int8_t left;        /* box position relative to the pen (negative: starts left of it) */
    uint8_t top;        /* first row with ink, counted from the top of the line */
    uint8_t rows;       /* rows with ink (0: nothing to draw, e.g. space) */
    uint32_t off;       /* first byte in data: w * rows coverage values, two per byte, high nibble first */
} gp5_glyph_t;

typedef struct {
    uint8_t h;          /* line height in pixels */
    uint8_t base;       /* baseline, pixels below the top of the line */
    const gp5_glyph_t *g;   /* one per character, 0x20 .. 0x7E */
    const uint8_t *data;
} gp5_font_t;

/* Colours: 0xRRGGBB. Pixels in buffers and gp5_blit(): gp5_color() gives the screen's own format. */
uint16_t gp5_color(uint32_t rgb);

/* Width of s in pixels. */
int gp5_text_width(const gp5_font_t *f, const char *s);

/* Draw s into a w x h pixel buffer of your own (screen format, row by row, e.g. a strip you then gp5_blit()),
 * the top of the line at x, y in that buffer. Text is blended into the pixels already there; anything outside
 * the buffer is clipped. Returns the x after the last character. */
int gp5_draw_text(uint16_t *buf, unsigned w, unsigned h, int x, int y, const gp5_font_t *f, const char *s,
                  uint32_t fg);

/* Draw s straight onto the screen: a strip of the line's height and the text's width is filled with bg, the
 * text drawn in fg, and sent. x, y = top left on the 160 x 80 screen; clipped at its edges. Returns the width. */
int gp5_text_at(int x, int y, const gp5_font_t *f, const char *s, uint32_t fg, uint32_t bg);
#endif
