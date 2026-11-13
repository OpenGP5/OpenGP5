/* Strobe tuner, a demo firmware built with OpenGP5. Control-processor side: the strobe tuner's screen and controls.
 *
 *   foot switch   mute on / off (tune silently); LED mint = sound on, dim red = muted
 *   PARA turn     reference pitch A4, 430..450 Hz (saved); PARA push: back to 440 Hz
 *   volume knob   output level
 *
 * The DSP finds the note and runs the strobe (dsp/tuner.h) and sends both over the audio link every block; this
 * side draws the note, the cents, and the strobe's three bands (fundamental, 2nd and 4th harmonic): light and
 * dark segments that stand still when the string is in tune and drift right when it is sharp, left when it is
 * flat. One colour throughout, whatever the tuning: the movement is the information.
 * Lettering: the platform's smooth "sans" font (gp5_font.h, derived from Varela) in three sizes. */
#include "gp5_firmware.h"
#include "gp5_font.h"
#include "fonts/sans_12.h"
#include "fonts/sans_20.h"
#include "fonts/sans_40.h"
#include "../dsp/link_words.h"

/* Pixel format: the panel is blue-green-red 5-6-5, high byte first. */
#define RGB565(r, g, b) ((uint16_t)((((b) >> 3) << 11) | (((g) >> 2) << 5) | ((r) >> 3)))
#define PIX(r, g, b)    ((uint16_t)((RGB565(r, g, b) >> 8) | (RGB565(r, g, b) << 8)))
typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t BG = { 10, 12, 18 }, INK = { 236, 240, 246 }, DIM = { 110, 118, 134 }, FAINT = { 30, 35, 46 };
static uint16_t pix(rgb_t c) { return PIX(c.r, c.g, c.b); }
static rgb_t mix(rgb_t a, rgb_t b, unsigned t)                   /* a .. b, t = 0..255 */
{
    rgb_t c;
    c.r = (uint8_t)(a.r + (((int)b.r - (int)a.r) * (int)t) / 255);
    c.g = (uint8_t)(a.g + (((int)b.g - (int)a.g) * (int)t) / 255);
    c.b = (uint8_t)(a.b + (((int)b.b - (int)a.b) * (int)t) / 255);
    return c;
}

/* ---- what the DSP sends (written in the link interrupt) ---- */
static volatile int r_note = -1, r_cents;
static volatile uint32_t r_seg[48], r_seen;                 /* 3 bands x 64 segments, four bytes per word */
static volatile unsigned a4x10 = 4400, muted;

static void on_block(const int32_t *in, int32_t *out)
{
    unsigned k;
    if ((uint32_t)in[LW_MAGIC] == LW_MAGIC_D) {
        r_note = in[LW_NOTE]; r_cents = in[LW_CENTS];
        for (k = 0; k < 48; k++) r_seg[k] = (uint32_t)in[LW_SEG + k];
        r_seen++;
    }
    out[LW_MAGIC] = (int32_t)LW_MAGIC_N;
    out[LW_A4] = (int32_t)a4x10;
    out[LW_MUTE] = (int32_t)muted;
}

/* ---- controls ---- */
#define STORE_ID 0x524E5554u                               /* "TUNR" */
static unsigned level, ticks, save_at, show_ref_until;

void fw_init(void)
{
    uint32_t v = 0;
    level = 0; muted = 0; ticks = 0; save_at = 0; show_ref_until = 0;
    if (gp5_store_load(STORE_ID, &v, sizeof v) == sizeof v && v >= 4300u && v <= 4500u) a4x10 = v;
    gp5_link_start(on_block);
}
void fw_foot(void) { muted ^= 1u; }
static void ref_changed(void) { save_at = ticks + 200u; show_ref_until = ticks + 200u; }   /* save after 2 s still */
void fw_para_push(void) { if (a4x10 != 4400u) { a4x10 = 4400u; ref_changed(); } else show_ref_until = ticks + 200u; }
void fw_para_turn(int detents)
{
    int v = (int)a4x10 + 10 * detents;
    if (v < 4300) v = 4300;
    if (v > 4500) v = 4500;
    if ((unsigned)v != a4x10) { a4x10 = (unsigned)v; ref_changed(); }
    else show_ref_until = ticks + 200u;
}
void fw_knob(unsigned knob)                                  /* every ~10 ms: also the clock for this file */
{
    uint32_t v;
    level = knob;
    ticks++;
    if (save_at && (int)(ticks - save_at) >= 0) { v = a4x10; gp5_store_save(STORE_ID, &v, sizeof v); save_at = 0; }
}
unsigned fw_level(void) { return level; }
void fw_dsp_ready(void) {}
int fw_take(uint32_t *id, uint32_t *value) { (void)id; (void)value; return 0; }
unsigned fw_setting_count(void) { return 0; }
unsigned fw_active(void) { return muted ? 0u : 1u; }

/* ---- drawing ---- */
#define W GP5_SCREEN_W
#define TOP_H 42u                                            /* note and cents */
#define STROBE_Y 46u
#define BAND_H 9u
#define BAND_GAP 3u
#define STROBE_H (3u * BAND_H + 2u * BAND_GAP)
static uint16_t band[W * TOP_H];

/* text by its baseline (gp5_draw_text takes the top of the line), blended into the band */
#define NOTE  (&gp5_font_sans_40)
#define MID   (&gp5_font_sans_20)
#define SMALL (&gp5_font_sans_12)
static uint32_t rgb(rgb_t c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }
static int text_w(const gp5_font_t *f, const char *s) { return gp5_text_width(f, s); }
static void text(int x, int baseline, const char *s, const gp5_font_t *f, rgb_t c)
{
    gp5_draw_text(band, W, TOP_H, x, baseline - (int)f->base, f, s, rgb(c));
}

static const char *const LETTER = "CCDDEFFGGAAB";
static const uint8_t SHARP_SIGN[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

static void draw_top(int note, int cents10)
{
    char s[12];
    unsigned i;
    uint16_t bg = pix(BG);
    for (i = 0; i < W * TOP_H; i++) band[i] = bg;
    if ((int)(ticks - show_ref_until) < 0) {                 /* the reference pitch, while it is being set */
        unsigned v = a4x10 / 10u;
        s[0] = 'A'; s[1] = '4'; s[2] = ' '; s[3] = (char)('0' + v / 100u); s[4] = (char)('0' + v / 10u % 10u);
        s[5] = (char)('0' + v % 10u); s[6] = 0;
        text(6, 31, s + 3, NOTE, INK);                        /* "442" */
        text(9 + text_w(NOTE, s + 3), 31, "Hz", SMALL, DIM);
        text(156 - text_w(SMALL, "A4 pitch"), 14, "A4 pitch", SMALL, DIM);
        text(156 - text_w(SMALL, "push: 440"), 39, "push: 440", SMALL, DIM);
    } else if (note < 0) {
        text(6, 31, "-", NOTE, FAINT);
        text(156 - text_w(SMALL, "play a string"), 24, "play a string", SMALL, DIM);
    } else {
        int n = note % 12, oct = note / 12 - 1, a = cents10 < 0 ? -cents10 : cents10, x;
        char l[2] = { LETTER[n], 0 }, o[2] = { (char)('0' + (oct < 0 ? 0 : oct > 9 ? 9 : oct)), 0 };
        text(6, 31, l, NOTE, INK);
        x = 8 + text_w(NOTE, l);
        if (SHARP_SIGN[n]) text(x, 15, "#", MID, INK);
        text(x, 37, o, MID, DIM);
        a = (a + 5) / 10;                                     /* whole cents, right-aligned */
        i = 0;
        if (a) s[i++] = cents10 > 0 ? '+' : '-';
        if (a >= 10) s[i++] = (char)('0' + a / 10);
        s[i++] = (char)('0' + a % 10);
        s[i] = 0;
        text(156 - text_w(NOTE, s), 31, s, NOTE, INK);
    }
    if (muted) text(70, 12, "muted", SMALL, INK);
    gp5_blit(0, 0, W, TOP_H, band);
}

/* the strobe: 64 segments per band across the width, each lit from 0 (background) to 255 (full ink); between
 * segment centres the brightness is interpolated, so moving edges glide instead of jumping 2.5 pixels */
static void draw_strobe(int note)
{
    unsigned b, x, y;
    uint16_t line[W];
    rgb_t lit = note < 0 ? FAINT : INK;
    for (b = 0; b < 3; b++) {
        for (x = 0; x < W; x++) {
            /* position in 1/256 segment, measured from the first segment's centre (+ one turn to stay positive) */
            unsigned q = (x * 256u * LW_SEG_N) / W + 256u * LW_SEG_N - 128u, g0, g1, f, v0, v1, v;
            g0 = (q >> 8) % LW_SEG_N; g1 = (g0 + 1u) % LW_SEG_N; f = q & 255u;
            v0 = (r_seg[(b * LW_SEG_N + g0) / 4] >> (8 * (g0 & 3u))) & 255u;
            v1 = (r_seg[(b * LW_SEG_N + g1) / 4] >> (8 * (g1 & 3u))) & 255u;
            v = (v0 * (256u - f) + v1 * f) >> 8;
            line[x] = pix(mix(BG, lit, v));
        }
        for (y = 0; y < BAND_H; y++) gp5_blit(0, STROBE_Y + b * (BAND_H + BAND_GAP) + y, W, 1, line);
    }
}

static void draw_gaps(void)
{
    static uint16_t line[W];
    unsigned x, k, j;
    for (x = 0; x < W; x++) line[x] = pix(BG);
    for (k = TOP_H; k < STROBE_Y; k++) gp5_blit(0, k, W, 1, line);
    for (k = 0; k < 2u; k++)
        for (j = 0; j < BAND_GAP; j++) gp5_blit(0, STROBE_Y + BAND_H + k * (BAND_H + BAND_GAP) + j, W, 1, line);
    for (k = STROBE_Y + STROBE_H; k < GP5_SCREEN_H; k++) gp5_blit(0, k, W, 1, line);
}

int fw_draw(unsigned knob, int force)
{
    static int shown_note = -2, shown_cents = 9999;
    static unsigned shown_mute = 9, shown_ref = 0, ref_vis = 0, last_strobe = 0;
    int note = r_note, cents10 = r_cents;
    unsigned rv = (int)(ticks - show_ref_until) < 0;
    (void)knob;
    if (force) { draw_gaps(); shown_note = -2; }
    if (force || note != shown_note || (cents10 + 1000) / 5 != (shown_cents + 1000) / 5 || muted != shown_mute ||
        rv != ref_vis || (rv && a4x10 != shown_ref)) {
        draw_top(note, cents10);
        shown_note = note; shown_cents = cents10; shown_mute = muted; ref_vis = rv; shown_ref = a4x10;
        return 1;
    }
    if (force || ticks != last_strobe) {                      /* the strobe: up to 100 frames a second */
        last_strobe = ticks;
        draw_strobe(note);
    }
    return 0;
}
