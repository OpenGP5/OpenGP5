/* GP-5 open platform: text in the fonts of platform/nds32/fonts/ (gp5_font.h). */
#include "gp5_font.h"
#include "gp5_firmware.h"

/* The panel takes blue-green-red 5-6-5, high byte first (UI_PANEL builds); elsewhere (GP5_HOST previews on the
 * computer) plain red-green-blue 5-6-5. */
static uint16_t pack(unsigned r, unsigned g, unsigned b)
{
#ifdef UI_PANEL
    uint16_t v = (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
#else
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
#endif
}

static void unpack(uint16_t p, unsigned *r, unsigned *g, unsigned *b)
{
#ifdef UI_PANEL
    p = (uint16_t)((p >> 8) | (p << 8));
    *b = (p >> 11) & 31u; *g = (p >> 5) & 63u; *r = p & 31u;
#else
    *r = (p >> 11) & 31u; *g = (p >> 5) & 63u; *b = p & 31u;
#endif
    *r = (*r << 3) | (*r >> 2); *g = (*g << 2) | (*g >> 4); *b = (*b << 3) | (*b >> 2);
}

uint16_t gp5_color(uint32_t rgb) { return pack((rgb >> 16) & 255u, (rgb >> 8) & 255u, rgb & 255u); }

static const gp5_glyph_t *glyph(const gp5_font_t *f, char c)
{
    unsigned u = (unsigned char)c;
    if (u < 0x20u || u > 0x7Eu) u = '?';
    return &f->g[u - 0x20u];
}

int gp5_text_width(const gp5_font_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++) w += glyph(f, *s)->adv;
    return w;
}

int gp5_draw_text(uint16_t *buf, unsigned w, unsigned h, int x, int y, const gp5_font_t *f, const char *s,
                  uint32_t fg)
{
    unsigned fr = (fg >> 16) & 255u, fgg = (fg >> 8) & 255u, fb = fg & 255u;
    for (; *s; s++) {
        const gp5_glyph_t *g = glyph(f, *s);
        const uint8_t *d = f->data + g->off;
        int gx = x + g->left;
        unsigned i, j;
        if (gx < (int)w && gx + g->w > 0)
            for (j = 0; j < g->rows; j++) {
                int py = y + (int)g->top + (int)j;
                if (py < 0 || py >= (int)h) continue;
                for (i = 0; i < g->w; i++) {
                    unsigned k = j * g->w + i, a = (k & 1u) ? d[k >> 1] & 15u : d[k >> 1] >> 4;
                    int px = gx + (int)i;
                    uint16_t *p;
                    unsigned r, gg, b;
                    if (!a || px < 0 || px >= (int)w) continue;
                    p = &buf[(unsigned)py * w + (unsigned)px];
                    if (a == 15u) { *p = pack(fr, fgg, fb); continue; }
                    unpack(*p, &r, &gg, &b);
                    *p = pack(r + ((int)fr - (int)r) * (int)a / 15, gg + ((int)fgg - (int)gg) * (int)a / 15,
                              b + ((int)fb - (int)b) * (int)a / 15);
                }
            }
        x += g->adv;
    }
    return x;
}

#define STRIP_H 40u
static uint16_t strip[GP5_SCREEN_W * STRIP_H];

int gp5_text_at(int x, int y, const gp5_font_t *f, const char *s, uint32_t fg, uint32_t bg)
{
    int tw = gp5_text_width(f, s), x0 = x < 0 ? 0 : x, x1 = x + tw, y0, y1;
    unsigned n, i, sw;
    uint16_t c = gp5_color(bg);
    if (x1 > (int)GP5_SCREEN_W) x1 = GP5_SCREEN_W;
    if (x0 >= x1) return tw;
    sw = (unsigned)(x1 - x0);
    for (y0 = y < 0 ? 0 : y; y0 < y + (int)f->h && y0 < (int)GP5_SCREEN_H; y0 = y1) {   /* strips of STRIP_H rows */
        y1 = y0 + (int)STRIP_H;
        if (y1 > y + (int)f->h) y1 = y + (int)f->h;
        if (y1 > (int)GP5_SCREEN_H) y1 = GP5_SCREEN_H;
        n = sw * (unsigned)(y1 - y0);
        for (i = 0; i < n; i++) strip[i] = c;
        gp5_draw_text(strip, sw, (unsigned)(y1 - y0), x - x0, y - y0, f, s, fg);
        gp5_blit((unsigned)x0, (unsigned)y0, sw, (unsigned)(y1 - y0), strip);
    }
    return tw;
}
