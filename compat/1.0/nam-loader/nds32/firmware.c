/* NAM A2-Lite capture player, control-processor UI. The bank and the switching are in a2.c (nam.h).
 *
 *   turn PARA     NAM page (the capture playing) <-> CAB page (its IR); in a list: move
 *   push PARA     the list of the page shown: the capture bank (NAM) or the IRs (CAB, with NONE); in a list: load
 *                 the highlighted one (the capture playing keeps playing while you browse); after 5 s still: back
 *   hold PARA     the TONE page of the capture playing (0.6 s; again to leave, or 10 s untouched):
 *                 turn to pick GATE / BASS / MID / TREBLE, push to adjust it (turn), push again to pick
 *   foot switch   bypass
 *   volume knob   output level
 * Each tone keeps its own gate, EQ (../tone.h) and IR, saved with the capture's checksum: a new capture in a slot
 * starts with the gate off, the EQ flat and the IR of the same number if there is one (as before 1.3, when tone NN
 * always played cab-NN), none otherwise. The tone playing's values are also parameters (../params.h, OpenGP5 1.0),
 * so a computer reads and sets them (Launchpad's Gate and EQ card, gp5_link.py set Gate 30).
 *
 * Screen (160 x 80, yellow on near-black, the platform's sans font):
 *   header  NAM or CAB (two dots: the page) / BYPASS / LOADING nn / BANK / CABS; on the right slot nn / 30, IR nn / 12
 *   name    the capture's name (NAM) or the IR's (CAB), large; a name wider than the screen slides to its end and back
 *   footer  input meter and the volume; while switching, a progress bar
 * The slot playing is remembered across power-off (the platform's store). */
#include "gp5_firmware.h"
#include "gp5_params.h"
#include "gp5_fmt.h"
#include "gp5_font.h"
#include "fonts/sans_12.h"
#include "fonts/sans_16.h"
#include "fonts/sans_32.h"
#include "nam.h"

#define STORE_ID 0x42504D41u                         /* "AMPB": the slot playing */
#define STORE_TONE 0x54504D41u                       /* "AMPT": each tone's gate and EQ */
#define EDIT_TICKS 1000u                             /* 10 s untouched: back */
#define SAVE_TICKS 200u                              /* a changed setting is saved 2 s after the last change */
#define W GP5_SCREEN_W
#define HEAD_H 16
#define NAME_Y HEAD_H
#define NAME_H 44
#define FOOT_Y (NAME_Y + NAME_H)
#define FOOT_H (GP5_SCREEN_H - FOOT_Y)
#define ROW_H 21
#define BROWSE_TICKS 500u                            /* 5 s */
#define LOADING_MIN 30u                              /* show "loading" at least 0.3 s */
#define PAUSE_TICKS 150u                             /* scrolling name: 1.5 s at each end */
#define SCROLL_TICKS_PER_PX 5u                       /* 20 px a second */
#define MARGIN 6

typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t BG = { 12, 11, 8 }, YELLOW = { 255, 199, 44 }, INK = { 244, 242, 236 }, DIM = { 112, 110, 104 },
                   GHOST = { 78, 76, 72 }, TRACK = { 40, 38, 34 }, ONYEL = { 28, 22, 4 };
static uint32_t rgb(rgb_t c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }

/* ---- controls ---- */
static unsigned ticks, browsing, browse_until, loading_until, show_empty_until;
static int cursor;

/* ---- each tone's gate, EQ and IR: [0..29] the slots, [30] the built-in capture; a setting belongs to a checksum ---- */
typedef struct { uint32_t crc; uint8_t v[4]; uint8_t ir, pad[3]; } tone_rec_t;   /* gate, bass, mid, treble (tone.h's
                                                     ranges); ir: 0 the default (below), 1 none, 2.. IR 1.. */
typedef struct { uint32_t crc; uint8_t v[4]; } tone_rec12_t;    /* before 1.3: 12 slots + the built-in one */
static tone_rec_t trec[NAM_SLOTS + 1];
static const uint8_t tmax[4] = { NAM_GATE_MAX, NAM_EQ_MAX, NAM_EQ_MAX, NAM_EQ_MAX };
static unsigned save_due, editing, edit_row, adjusting, edit_until;
static unsigned page;                                /* 0 NAM, 1 CAB */
static unsigned blist;                               /* the list being browsed: 0 captures, 1 IRs (row 0 NONE) */
static uint32_t tone_key = 1;                        /* the capture whose settings are applied (1: none yet) */

static unsigned tidx(void) { int c = nam_current(); return c < 0 ? NAM_SLOTS : (unsigned)c; }
static void tone_get(uint8_t v[4])
{
    tone_rec_t *t = &trec[tidx()];
    if (t->crc == nam_current_crc() && t->crc) { v[0] = t->v[0]; v[1] = t->v[1]; v[2] = t->v[2]; v[3] = t->v[3]; }
    else { v[0] = 0; v[1] = v[2] = v[3] = NAM_EQ_FLAT; }
}
static void tone_params(const uint8_t v[4])        /* the parameters show the tone playing's values */
{
    gp5_param_set(GP5_P_GATE, v[0]); gp5_param_set(GP5_P_BASS, v[1]); gp5_param_set(GP5_P_MID, v[2]); gp5_param_set(GP5_P_TREBLE, v[3]);
}
static void tone_apply(void) { uint8_t v[4]; tone_get(v); nam_tone(v[0], v[1], v[2], v[3]); tone_params(v); tone_key = nam_current_crc(); }
static tone_rec_t *tone_own(void)                   /* the tone playing's record, started afresh for a new capture */
{
    tone_rec_t *t = &trec[tidx()];
    if (t->crc != nam_current_crc() || !t->crc) {
        uint8_t v[4];
        tone_get(v);
        t->crc = nam_current_crc(); t->v[0] = v[0]; t->v[1] = v[1]; t->v[2] = v[2]; t->v[3] = v[3]; t->ir = 0;
    }
    return t;
}
static void save_soon(void) { save_due = ticks + SAVE_TICKS; if (!save_due) save_due = 1; }
/* the IR the tone playing plays (-1: none): its choice, or by default the one of the same number */
static int ir_want(void)
{
    const tone_rec_t *t = &trec[tidx()];
    int c = nam_current();
    if (t->crc == nam_current_crc() && t->crc && t->ir) return t->ir >= 2 && nam_ir_ok(t->ir - 2) ? t->ir - 2 : -1;
    return c >= 0 && c < (int)NAM_IRS && nam_ir_ok(c) ? c : -1;
}
static void ir_choose(int i)                         /* -1: none */
{
    tone_own()->ir = (uint8_t)(i < 0 ? 1 : i + 2);
    nam_ir_select(ir_want());
    gp5_param_set(GP5_P_IR, nam_ir_current() + 1);
    save_soon();
}
static void tone_change(unsigned k, unsigned value)  /* one setting of the tone playing */
{
    uint8_t v[4];
    tone_rec_t *t = tone_own();
    v[0] = t->v[0]; v[1] = t->v[1]; v[2] = t->v[2]; v[3] = t->v[3];
    v[k] = (uint8_t)(value > tmax[k] ? tmax[k] : value);
    for (unsigned i = 0; i < 4; i++) t->v[i] = v[i];
    nam_tone(v[0], v[1], v[2], v[3]);
    tone_params(v);
    save_soon();
}
static void tone_load(void)
{
    static tone_rec12_t old[13];
    unsigned i, n = gp5_store_load(STORE_TONE, trec, sizeof trec);
    if (n == sizeof trec) { tone_key = 1; return; }  /* applied on the next tick */
    for (i = 0; i <= NAM_SLOTS; i++) { trec[i].crc = 0; trec[i].ir = 0; }
    if (n == sizeof old && gp5_store_load(STORE_TONE, old, sizeof old) == sizeof old)   /* saved by 1.2: kept */
        for (i = 0; i < 13; i++) {
            tone_rec_t *t = &trec[i < 12 ? i : NAM_SLOTS];
            t->crc = old[i].crc; t->v[0] = old[i].v[0]; t->v[1] = old[i].v[1]; t->v[2] = old[i].v[2]; t->v[3] = old[i].v[3];
        }
    tone_key = 1;
}

static void save_slot(int s) { int32_t v = s; gp5_store_save(STORE_ID, &v, sizeof v); }
static void choose(int s)
{
    if (s == nam_current()) return;
    nam_select(s);
    save_slot(s);
    loading_until = ticks + LOADING_MIN;
}

void fw_init(void)
{
    int32_t v = -1;
    ticks = 0; browsing = 0; nam_bypass = 0;
    if (gp5_store_load(STORE_ID, &v, sizeof v) != sizeof v) v = -1;
    save_due = editing = adjusting = 0;
    gp5_led(GP5_LED_RGB(32, 10, 0));                 /* yellow; dim red in bypass */
    tone_load();
    nam_start((int)v);
    cursor = nam_current() < 0 ? 0 : nam_current();
}
void fw_foot_down(void) { nam_bypass ^= 1u; gp5_led(nam_bypass ? GP5_LED_BYPASS : GP5_LED_RGB(32, 10, 0)); }
void fw_storage_changed(void) { tone_load(); }       /* tone settings from a backup; tone files: nam_poll picks them up */
/* a parameter changed from the computer (gp5_params.h; ids as Launchpad has used them since 1.1) */
void fw_param_changed(unsigned id)
{
    if (id == GP5_P_CAB) nam_cab(gp5_param(GP5_P_CAB) != 0);
    if (id == GP5_P_TAPS) nam_cab_limit((unsigned)gp5_param(GP5_P_TAPS));
    if (id >= GP5_P_GATE && id <= GP5_P_TREBLE) tone_change(id - GP5_P_GATE, (unsigned)gp5_param(id));
    if (id == GP5_P_IR) ir_choose((int)gp5_param(GP5_P_IR) - 1);
    if (id == GP5_P_SLOT) {                          /* switch, as a push in the bank does (an empty slot: no change) */
        int s = (int)gp5_param(GP5_P_SLOT) - 1;
        if (s < 0 || nam_slot_ok(s)) { choose(s); browsing = 0; }
        gp5_param_set(GP5_P_SLOT, nam_current() + 1);   /* (until the switch is done: the one playing) */
    }
}
int fw_param_text(unsigned id, int32_t v, char *t)  /* "Off", "-55 dB", "+3 dB", "all", "512 taps" */
{
    unsigned n = 0, a;
    int d;
    if (id == GP5_P_TAPS) {
        if (!v) { t[0] = 'a'; t[1] = 'l'; t[2] = 'l'; t[3] = 0; return 1; }
        gp5_fmt(t, 16, "%ld taps", (long)v); return 1;
    }
    if (id == GP5_P_SLOT) {                          /* "built-in", or the capture's name (15 characters at most) */
        const char *s = v > 0 && nam_slot_ok((int)v - 1) ? nam_slot_name((int)v - 1) : v > 0 ? "empty" : "built-in";
        for (n = 0; n < 15 && s[n]; n++) t[n] = s[n];
        t[n] = 0; return 1;
    }
    if (id == GP5_P_IR) {                            /* "none", or the IR's name (15 characters at most) */
        const char *s = v > 0 && nam_ir_ok((int)v - 1) ? nam_ir_name((int)v - 1) : "none";
        for (n = 0; n < 15 && s[n]; n++) t[n] = s[n];
        t[n] = 0; return 1;
    }
    if (id == GP5_P_GATE && !v) { t[0] = 'O'; t[1] = 'f'; t[2] = 'f'; t[3] = 0; return 1; }
    if (id < GP5_P_GATE || id > GP5_P_TREBLE) return 0;
    d = id == GP5_P_GATE ? -(91 - (int)v) : (int)v - (int)NAM_EQ_FLAT;
    a = (unsigned)(d < 0 ? -d : d);
    if (d) t[n++] = d < 0 ? '-' : '+';
    if (a >= 10) t[n++] = (char)('0' + a / 10);
    t[n++] = (char)('0' + a % 10); t[n++] = ' '; t[n++] = 'd'; t[n++] = 'B'; t[n] = 0;
    return 1;
}

/* PARA: a click (let go before 0.6 s); a hold opens or closes the TONE page (the platform tells them apart) */
static int list_len(void) { return blist ? (int)NAM_IRS + 1 : (int)NAM_SLOTS; }
void fw_para_click(void)
{
    if (editing) { adjusting ^= 1u; edit_until = ticks + EDIT_TICKS; return; }
    if (!browsing) {                                 /* the list of the page shown, at the one playing */
        browsing = 1; blist = page;
        cursor = blist ? nam_ir_current() + 1 : (nam_current() < 0 ? 0 : nam_current());
        browse_until = ticks + BROWSE_TICKS; return;
    }
    if (blist) {                                     /* row 0: NONE; row r: IR r */
        if (cursor > 0 && !nam_ir_ok(cursor - 1)) { show_empty_until = ticks + 100u; browse_until = ticks + BROWSE_TICKS; return; }
        ir_choose(cursor - 1);
        if (cursor > 0 && !nam_cab_is_on()) { nam_cab(1); gp5_param_set(GP5_P_CAB, 1); }   /* an IR picked: heard */
        browsing = 0;
        return;
    }
    if (!nam_slot_ok(cursor)) { show_empty_until = ticks + 100u; browse_until = ticks + BROWSE_TICKS; return; }
    choose(cursor);
    browsing = 0;
}
void fw_para_hold(void) { browsing = 0; adjusting = 0; editing ^= 1u; edit_row = 0; edit_until = ticks + EDIT_TICKS; }
void fw_para_turn(int detents)
{
    if (editing) {
        edit_until = ticks + EDIT_TICKS;
        if (adjusting) {
            uint8_t v[4]; int x;
            tone_get(v);
            x = (int)v[edit_row] + detents;
            tone_change(edit_row, x < 0 ? 0u : (unsigned)x);
        } else edit_row = (unsigned)(((int)edit_row + detents) % 4 + 4) % 4;
        return;
    }
    if (!browsing) { page = (unsigned)(((int)page + detents) % 2 + 2) % 2; return; }   /* NAM <-> CAB */
    cursor = ((cursor + detents) % list_len() + list_len()) % list_len();
    browse_until = ticks + BROWSE_TICKS;
}
void fw_tick(void)                                   /* every 10 ms: the clock for this file */
{
    int got;
    ticks++;
    if (browsing && (int)(ticks - browse_until) >= 0) browsing = 0;
    if (gp5_param(GP5_P_CAB) != nam_cab_is_on()) gp5_param_set(GP5_P_CAB, nam_cab_is_on());   /* (the IR may switch itself off) */
    if (editing && (int)(ticks - edit_until) >= 0) { editing = 0; adjusting = 0; }
    got = nam_poll();
    if (got >= 0) { save_slot(got); browsing = 0; loading_until = ticks + LOADING_MIN; }   /* an upload landed */
    if (nam_current_crc() != tone_key && !nam_busy()) tone_apply();       /* another capture: its settings */
    if (!nam_busy() && ir_want() != nam_ir_current()) nam_ir_select(ir_want());   /* its IR (or one just uploaded) */
    if (gp5_param(GP5_P_IR) != nam_ir_current() + 1) gp5_param_set(GP5_P_IR, nam_ir_current() + 1);
    if (!nam_busy() && gp5_param(GP5_P_SLOT) != nam_current() + 1) gp5_param_set(GP5_P_SLOT, nam_current() + 1);
    if (save_due && (int)(ticks - save_due) >= 0) { save_due = 0; gp5_store_save(STORE_TONE, trec, sizeof trec); }
}

/* ---- drawing ---- */
static uint16_t band[W * NAME_H];
static unsigned bh;                                  /* rows of the band in use */

static void fill(rgb_t c) { unsigned i; uint16_t p = gp5_color(rgb(c)); for (i = 0; i < W * bh; i++) band[i] = p; }
static void box(int x, int y, int w, int h, rgb_t c)
{
    int i, j; uint16_t p = gp5_color(rgb(c));
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++)
        if (i >= 0 && i < W && j >= 0 && j < (int)bh) band[j * W + i] = p;
}
static int text(int x, int baseline, const gp5_font_t *f, const char *s, rgb_t c)
{
    return gp5_draw_text(band, W, bh, x, baseline - (int)f->base, f, s, rgb(c));
}
static void num2(char *p, int v) { p[0] = (char)('0' + v / 10 % 10); p[1] = (char)('0' + v % 10); }

static void draw_head(unsigned mode)                 /* mode: 0 play, 1 bypass, 2 loading, 3 browse, 4 tone */
{
    char r[10] = "00 / 00", l[12] = "LOADING 00";
    const gp5_font_t *f = &gp5_font_sans_12;
    bh = HEAD_H; fill(BG);
    if (mode == 4) {
        const char *h = adjusting ? "turn to set" : "push to adjust";
        text(MARGIN, 12, f, "TONE", YELLOW);
        text(W - MARGIN - gp5_text_width(f, h), 12, f, h, DIM);
    } else if (mode == 3) {
        const char *h = blist ? "push to choose" : "push to load";
        text(MARGIN, 12, f, blist ? "CABS" : "BANK", YELLOW);
        text(W - MARGIN - gp5_text_width(f, h), 12, f, h, DIM);
    } else {
        int s = page ? nam_ir_current() : nam_current(), x;
        const char *lab = mode == 1 ? "BYPASS" : page ? "CAB" : "NAM";
        if (mode == 2) { num2(l + 8, nam_current() + 1); text(MARGIN, 12, f, nam_current() < 0 ? "LOADING" : l, YELLOW); }
        else {
            x = text(MARGIN, 12, f, lab, mode == 1 ? DIM : YELLOW);
            if (mode == 0) {                         /* the page: two dots, this one lit (turn PARA for the other) */
                box(x + 6, 6, 4, 4, page ? TRACK : YELLOW); box(x + 12, 6, 4, 4, page ? YELLOW : TRACK);
            }
        }
        if (s < 0) { const char *e = page ? "none" : "built-in"; text(W - MARGIN - gp5_text_width(f, e), 12, f, e, DIM); }
        else { num2(r, s + 1); num2(r + 5, page ? (int)NAM_IRS : NAM_SLOTS); text(W - MARGIN - gp5_text_width(f, r), 12, f, r, DIM); }
    }
    gp5_blit(0, 0, W, HEAD_H, band);
}

/* the name, large, at offset `off` pixels to the left (scrolling). Drawn and sent in NAME_STRIPS strips, one per
 * main-loop pass: the whole area at once (14 KB to the screen) made passes of 13-18 ms, during which USB waited.
 * A frame (name, offset, colour) is finished before the next one starts, so a strip never shows another offset. */
#define NAME_STRIPS 4u
#define STRIP_ROWS (NAME_H / NAME_STRIPS)
static struct { const char *s; int off; rgb_t c; unsigned next; } nf = { 0, 0, { 0, 0, 0 }, NAME_STRIPS };
static void name_frame(const char *s, int off, rgb_t c) { nf.s = s; nf.off = off; nf.c = c; nf.next = 0; }
static int name_strip(void)                          /* the next strip of the frame; 1 while more are left */
{
    unsigned k = nf.next;
    if (k >= NAME_STRIPS) return 0;
    bh = STRIP_ROWS; fill(BG);
    gp5_draw_text(band, W, STRIP_ROWS, MARGIN - nf.off, 34 - (int)gp5_font_sans_32.base - (int)(k * STRIP_ROWS),
                  &gp5_font_sans_32, nf.s, rgb(nf.c));
    box(0, 0, MARGIN, STRIP_ROWS, BG); box(W - MARGIN, 0, MARGIN, STRIP_ROWS, BG);   /* framed by the margins */
    gp5_blit(0, NAME_Y + k * STRIP_ROWS, W, STRIP_ROWS, band);
    return ++nf.next < NAME_STRIPS;
}

/* where a scrolling name is: pause, slide (eased), pause, slide back; 0 when it fits */
static int scroll_off(const char *s, unsigned t)
{
    int d = gp5_text_width(&gp5_font_sans_32, s) - (W - 2 * MARGIN);
    unsigned mv, per, p, x;
    if (d <= 0) return 0;
    mv = (unsigned)d * SCROLL_TICKS_PER_PX; per = 2u * (PAUSE_TICKS + mv); p = t % per;
    if (p < PAUSE_TICKS) return 0;
    if (p < PAUSE_TICKS + mv) x = p - PAUSE_TICKS;
    else if (p < 2u * PAUSE_TICKS + mv) return d;
    else x = mv - (p - 2u * PAUSE_TICKS - mv);
    {   /* smoothstep: 3x^2 - 2x^3, x = 0..1 in 1/4096 */
        uint32_t u = (uint32_t)((uint64_t)x * 4096u / mv), e = (u * u >> 12) * (3u * 4096u - 2u * u) >> 12;
        return (int)(((uint32_t)d * e + 2048u) >> 12);
    }
}

/* input meter (-48 .. 0 dBFS) and the volume; or a progress bar while switching */
static unsigned meter;                               /* 0..128, falls slowly */
static void draw_foot(unsigned mode)
{
    char v[8] = "VOL 100";
    unsigned pct = (gp5_knob() * 100u + 127u) / 255u, w, p = nam_take_peak(), l16 = 0, m;
    bh = FOOT_H; fill(BG);
    if (p) { unsigned b = 31u - (unsigned)__builtin_clz(p); l16 = b * 16u + (b >= 4 ? (p >> (b - 4)) & 15u : 0u); }
    m = l16 > 23u * 16u ? l16 - 23u * 16u : 0u;       /* log2 above 2^23 (-48 dB) in 1/16 */
    if (m > 128u) m = 128u;
    meter = m >= meter ? m : (meter > 3u ? meter - 3u : 0u);
    if (mode == 2) {
        w = (W - 2u * MARGIN) * nam_progress() / 255u;
        box(MARGIN, 8, W - 2 * MARGIN, 4, TRACK); box(MARGIN, 8, (int)w, 4, YELLOW);
    } else {
        int tx;
        v[4] = pct >= 100 ? '1' : ' '; v[5] = pct >= 10 ? (char)('0' + pct / 10 % 10) : ' '; v[6] = (char)('0' + pct % 10);
        tx = W - MARGIN - gp5_text_width(&gp5_font_sans_12, "VOL 100");
        text(tx, 14, &gp5_font_sans_12, v, DIM);
        w = (unsigned)(tx - 6 - MARGIN);
        box(MARGIN, 7, (int)w, 4, TRACK);
        box(MARGIN, 7, (int)(w * meter / 128u), 4, mode == 1 ? GHOST : YELLOW);
    }
    gp5_blit(0, FOOT_Y, W, FOOT_H, band);
}

/* the bank: three rows around the cursor, the highlighted one filled */
/* the bank: three rows (the one picked in the middle) and the row of background below them, drawn and sent one per
 * main-loop pass (all at once was 20 KB to the screen in one pass). A frame is drawn for the cursor it started with. */
static struct { int cursor; unsigned next; } bf = { 0, 4u };
static void row_cap(int s, const char **name, int *ok, int *playing)
{
    *ok = nam_slot_ok(s); *name = *ok ? nam_slot_name(s) : "empty"; *playing = s == nam_current();
}
static void row_ir(int r, const char **name, int *ok, int *playing)   /* row 0: NONE; row r: IR r */
{
    *ok = r == 0 || nam_ir_ok(r - 1);
    *name = r == 0 ? "none" : *ok ? nam_ir_name(r - 1) : "empty";
    *playing = r == nam_ir_current() + 1;
}
static int bank_row(void)                            /* the next row of the frame; 1 while more are left */
{
    unsigned k = bf.next;
    if (k >= 4u) return 0;
    if (k < 3u) {
        int len = list_len(), s = (bf.cursor + (int)k - 1 + len) % len, hl = k == 1u, ok, playing;
        char n[4] = "00 ";
        const char *name;
        if (blist) row_ir(s, &name, &ok, &playing); else row_cap(s, &name, &ok, &playing);
        if (!ok && hl && (int)(ticks - show_empty_until) < 0) name = "nothing to load";
        bh = ROW_H; fill(hl ? YELLOW : BG);
        if (blist && s == 0) { n[0] = '-'; n[1] = '-'; } else num2(n, blist ? s : s + 1);
        text(MARGIN, 16, &gp5_font_sans_16, n, hl ? ONYEL : DIM);
        text(MARGIN + 26, 16, &gp5_font_sans_16, name, hl ? ONYEL : (ok ? INK : GHOST));
        box(W - MARGIN - 12, 0, MARGIN + 12, ROW_H, hl ? YELLOW : BG);           /* long names end before the dot */
        if (playing) box(W - MARGIN - 5, 8, 5, 5, hl ? ONYEL : YELLOW);   /* the one playing */
        gp5_blit(0, HEAD_H + k * ROW_H, W, ROW_H, band);
    } else {                                                                      /* the row left at the bottom */
        bh = GP5_SCREEN_H - HEAD_H - 3u * ROW_H; fill(BG);
        gp5_blit(0, HEAD_H + 3u * ROW_H, W, bh, band);
    }
    return ++bf.next < 4u;
}

/* the TONE page: GATE, BASS, MID, TREBLE of the capture playing, a row each (16 px), drawn one at a time */
#define TROW_H 16
static void tone_text(char *b, unsigned k, unsigned v)   /* "OFF", "-55 dB", "+3 dB", "0 dB" */
{
    int d = k == 0 ? -(91 - (int)v) : (int)v - (int)NAM_EQ_FLAT, a = d < 0 ? -d : d, n = 0;
    if (k == 0 && v == 0) { b[0] = 'O'; b[1] = 'F'; b[2] = 'F'; b[3] = 0; return; }
    if (d) b[n++] = d < 0 ? '-' : '+';
    if (a >= 10) b[n++] = (char)('0' + a / 10);
    b[n++] = (char)('0' + a % 10); b[n++] = ' '; b[n++] = 'd'; b[n++] = 'B'; b[n] = 0;
}
static void draw_tone_row(unsigned k, const uint8_t v[4])
{
    static const char *const lab[4] = { "GATE", "BASS", "MID", "TREBLE" };
    char val[12], show[16];
    const gp5_font_t *f = &gp5_font_sans_12;
    int sel = k == edit_row, adj = sel && adjusting, i = 0, j = 0;
    rgb_t fg = sel && !adj ? ONYEL : adj ? YELLOW : DIM, fv = sel && !adj ? ONYEL : adj ? YELLOW : INK;
    tone_text(val, k, v[k]);
    if (adj) show[i++] = '<', show[i++] = ' ';
    while (val[j]) show[i++] = val[j++];
    if (adj) show[i++] = ' ', show[i++] = '>';
    show[i] = 0;
    bh = TROW_H; fill(sel && !adj ? YELLOW : BG);
    if (adj) { box(0, 0, 3, TROW_H, YELLOW); }
    text(MARGIN, 12, f, lab[k], fg);
    text(W - MARGIN - gp5_text_width(f, show), 12, f, show, fv);
    gp5_blit(0, HEAD_H + (int)k * TROW_H, W, TROW_H, band);
}

int fw_draw(int force)
{
    static unsigned sh_mode = 99, sh_cur = 99, sh_cursor = 99, sh_emp, sh_foot, part;
    static int sh_off = -1;
    static unsigned name_t0;                         /* a new name starts its scrolling from the start */
    static uint32_t sh_tone = 0xFFFFFFFFu;
    unsigned mode = editing ? 4u : browsing ? 3u : (nam_busy() || (int)(ticks - loading_until) < 0) ? 2u : nam_bypass ? 1u : 0u;
    unsigned cur = (unsigned)(nam_current() + 1) | (unsigned)(nam_ir_current() + 1) << 8 | page << 16 | blist << 17 | (unsigned)nam_cab_is_on() << 18,
             emp = (int)(ticks - show_empty_until) < 0;
    if (force || mode != sh_mode || cur != sh_cur) {
        if (cur != sh_cur || mode == 2) name_t0 = ticks;
        draw_head(mode); sh_mode = mode; sh_cur = cur;
        sh_off = -1; sh_cursor = 99; sh_tone = 0xFFFFFFFFu;   /* the rest follows on the next passes */
        nf.next = NAME_STRIPS;                       /* (a name frame half sent is started again) */
        bf.next = 4u;                                /* (and a bank frame) */
        return 1;
    }
    if (mode == 4) {                                 /* the TONE page: a changed row redrawn per pass */
        static unsigned sh_adj = 99, next;
        uint8_t v[4]; uint32_t key; unsigned k;
        tone_get(v);
        key = (uint32_t)v[0] | (uint32_t)v[1] << 8 | (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24;
        if (key != sh_tone || adjusting != sh_adj || edit_row != sh_cursor) {
            if (adjusting != sh_adj) { sh_adj = adjusting; draw_head(4); return 0; }   /* its hint */
            sh_tone = key; sh_cursor = edit_row; next = 0;
        }
        for (k = next; k < 4; k++) { draw_tone_row(k, v); next = k + 1; return 0; }
        return 0;
    }
    if (mode == 3) {
        if (bf.next < 4u) return bank_row();         /* the frame on its way first */
        if ((unsigned)cursor != sh_cursor || emp != sh_emp) { bf.cursor = cursor; bf.next = 0; sh_cursor = (unsigned)cursor; sh_emp = emp; return bank_row(); }
        return 0;
    }
    if (part++ & 1u) {                               /* name and footer take turns */
        const char *n = page ? (nam_ir_current() >= 0 ? nam_ir_name(nam_ir_current()) : "no IR") : nam_name();
        int off = scroll_off(n, ticks - name_t0);
        rgb_t c = mode == 1 || (page && (nam_ir_current() < 0 || !nam_cab_is_on())) ? GHOST : INK;   /* not heard: grey */
        if (nf.next < NAME_STRIPS) return name_strip();   /* the frame on its way first (more to do: say so) */
        if (off != sh_off) { name_frame(n, off, c); sh_off = off; return name_strip(); }
    } else if (ticks - sh_foot >= 3u) {              /* ~30 times a second */
        draw_foot(mode); sh_foot = ticks;
    }
    return 0;
}
