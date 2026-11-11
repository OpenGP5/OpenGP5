#include "ui.h"
#include "font5x7.h"

/* Pixel format. The pedal's panel is BGR 5-6-5 and takes the high byte first; with UI_PANEL the value
 * is stored so that the band buffer can be sent as it is. The host gets plain RGB 5-6-5. */
#ifdef UI_PANEL
#define RGB565(r, g, b) ((uint16_t)((((b) >> 3) << 11) | (((g) >> 2) << 5) | ((r) >> 3)))
#define UI_RGB(r, g, b) ((uint16_t)((RGB565(r, g, b) >> 8) | (RGB565(r, g, b) << 8)))
#else
#define UI_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#endif

#define C_BG     UI_RGB(10, 14, 20)
#define C_TEXT   UI_RGB(232, 238, 242)
#define C_DIM    UI_RGB(96, 112, 128)
#define C_FAINT  UI_RGB(44, 56, 68)
#define C_TRACK  UI_RGB(28, 38, 50)
#define C_ON     UI_RGB(46, 230, 166)     /* mint: effect on */
#define C_ICE    UI_RGB(110, 200, 255)    /* loop frozen */
#define C_LIVE   UI_RGB(255, 200, 87)     /* amber: following the big knob */

#define BAND_MAX 25
static uint16_t band[UI_W * BAND_MAX];
static unsigned by0, bh;                  /* band being drawn: first row, rows */

static void px(int x, int y, uint16_t c)
{
    if (x < 0 || x >= UI_W || y < (int)by0 || y >= (int)(by0 + bh)) return;
    band[(unsigned)(y - (int)by0) * UI_W + (unsigned)x] = c;
}

static void rect(int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) px(x + i, y + j, c);
}

/* filled rectangle with rounded ends (radius r) */
static void pill(int x, int y, int w, int h, int r, uint16_t c)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int dx = i < r ? r - 1 - i : (i >= w - r ? i - (w - r) : -1);
            int dy = j < r ? r - 1 - j : (j >= h - r ? j - (h - r) : -1);
            if (dx >= 0 && dy >= 0 && 4 * (dx * dx + dy * dy) > (2 * r - 1) * (2 * r - 1)) continue;
            px(x + i, y + j, c);
        }
    }
}

static int text_w(const char *s, int k)
{
    int n = 0;
    while (s[n]) n++;
    return n ? n * 6 * k - k : 0;
}

static void text(int x, int y, const char *s, int k, uint16_t c)
{
    for (; *s; s++, x += 6 * k) {
        unsigned ch = (unsigned char)*s, col, row;
        if (ch < 0x20 || ch > 0x5A) ch = '?';
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if ((font[ch - 0x20][col] >> row) & 1) rect(x + (int)col * k, y + (int)row * k, k, k, c);
    }
}

static uint32_t hash_str(uint32_t h, const char *s) { while (*s) h = h * 31u + (unsigned char)*s++; return h * 31u + 7u; }

static void begin(unsigned y0, unsigned h)
{
    unsigned i;
    by0 = y0; bh = h;
    for (i = 0; i < UI_W * h; i++) band[i] = C_BG;
}

int ui_draw(const ui_state_t *s, int force)
{
    static uint32_t last[5];
    static uint8_t valid[5];
    uint16_t accent = s->frozen ? C_ICE : C_ON;
    uint32_t h;
    int x, i;

    if (force) for (i = 0; i < 5; i++) valid[i] = 0;

    /* header, rows 0..12 */
    h = 1u + s->on + 2u * s->frozen;
    if (!valid[0] || h != last[0]) {
        last[0] = h; valid[0] = 1;
        begin(0, 13);
        text(6, 3, "GRANULAR", 1, s->on ? C_DIM : C_FAINT);
        if (s->frozen) { pill(98, 1, 27, 11, 5, C_ICE); text(103, 3, "FRZ", 1, C_BG); }
        if (s->on) { pill(131, 1, 23, 11, 5, C_ON); text(137, 3, "ON", 1, C_BG); }
        else { pill(127, 1, 27, 11, 5, C_FAINT); text(132, 3, "OFF", 1, C_DIM); }
        ui_blit(0, by0, UI_W, bh, band);
        return 1;
    }

    /* setting name, rows 13..30 */
    h = hash_str(s->on, s->name);
    if (!valid[1] || h != last[1]) {
        last[1] = h; valid[1] = 1;
        begin(13, 18);
        text(6, 15, s->name, 2, s->on ? C_TEXT : C_DIM);
        ui_blit(0, by0, UI_W, bh, band);
        return 1;
    }

    /* value, rows 31..55 */
    h = hash_str(s->on + 2u * s->live, s->value);
    if (!valid[2] || h != last[2]) {
        last[2] = h; valid[2] = 1;
        begin(31, 25);
        text(154 - text_w(s->value, 3), 33, s->value, 3, s->live ? C_LIVE : (s->on ? C_TEXT : C_DIM));
        ui_blit(0, by0, UI_W, bh, band);
        return 1;
    }

    /* bar, rows 56..68: track 148 px wide; knob marker underneath while the setting waits for the knob */
    h = s->frac + 256u * s->knob + 65536u * (s->live + 2u * s->on + 4u * s->frozen);
    if (!valid[3] || h != last[3]) {
        int fill = 6 + (int)(s->frac * 142u + 127u) / 255;     /* at least a round dot */
        int kx = 6 + 3 + (int)(s->knob * 142u + 127u) / 255;
        last[3] = h; valid[3] = 1;
        begin(56, 13);
        pill(6, 58, 148, 6, 3, C_TRACK);
        pill(6, 58, fill, 6, 3, s->live ? C_LIVE : (s->on ? accent : C_DIM));
        if (!s->live) { rect(kx - 1, 66, 3, 1, C_DIM); rect(kx, 65, 1, 1, C_DIM); rect(kx - 2, 67, 5, 1, C_DIM); }
        ui_blit(0, by0, UI_W, bh, band);
        return 1;
    }

    /* footer, rows 69..79: one dot per setting */
    h = s->sel + 256u * s->count + 65536u * (s->on + 2u * s->frozen);
    if (!valid[4] || h != last[4]) {
        last[4] = h; valid[4] = 1;
        begin(69, 11);
        x = (UI_W - ((int)s->count * 10 + 4 - 6)) / 2;
        for (i = 0; i < (int)s->count; i++) {
            if (i == (int)s->sel) { pill(x, 72, 8, 4, 2, s->on ? accent : C_DIM); x += 14; }
            else { pill(x, 72, 4, 4, 2, C_FAINT); x += 10; }
        }
        ui_blit(0, by0, UI_W, bh, band);
        return 1;
    }
    return 0;
}
