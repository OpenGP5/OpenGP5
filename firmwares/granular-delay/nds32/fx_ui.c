#include "fx_ui.h"

/* DSP parameter ids (fw-build/dsp_hello/granular.h) */
enum { D_MODE = 0, D_DELAY, D_SIZE, D_SPEED, D_SPRAY, D_FEEDBACK, D_MIX, D_CLOCK, D_LOOP, D_FREEZE, D_CHAOS,
       D_LOOPLV, D_LSPEED, D_N };
#define NO_DSP 255             /* a setting the DSP does not take as a 0x7F10 parameter */

/* What PARA steps through. Each is a step number lo..hi; value for the DSP = step * scale (or a table). */
enum { S_MIX, S_DELAY, S_GRAIN, S_SPEED, S_SPRAY, S_FEEDBACK, S_CHAOS, S_CLOCK, S_LOOP, S_LSPEED, S_LOOPLV, S_LEVEL, S_N };
static const struct { const char *name; uint8_t dsp; int16_t lo, hi, def; uint16_t scale; } par[S_N] = {
    { "MIX",    D_MIX,      0, 32, 16,   8 },     /* 0 dry only, 16 both full, 32 effect only */
    { "DELAY",  D_DELAY,    0, 51, 30, 441 },     /* 10 ms steps, 0 .. 510 ms (x CLOCK) */
    { "GRAIN",  D_SIZE,     1, 37, 20, 221 },     /* 5 ms steps, 5 .. 185 ms (x CLOCK) */
    { "SPEED",  D_SPEED,    0,  8,  5,   0 },     /* table below */
    { "SPRAY",  D_SPRAY,    0, 25,  4, 441 },     /* 10 ms steps, 0 .. 250 ms (x CLOCK) */
    { "FEEDBK", D_FEEDBACK, 0, 25, 11,  10 },     /* 0 .. 250 of 256 */
    { "CHAOS",  D_CHAOS,    0, 32,  8,   8 },     /* 0 .. 256 */
    { "CLOCK",  D_CLOCK,    1,  8,  1,   1 },
    { "LOOP",   D_LOOP,     1, 81, 50, 441 },     /* 10 ms steps, 10 .. 810 ms (x CLOCK) */
    { "LP SPD", D_LSPEED,   0,  8,  5,   0 },
    { "LP LVL", D_LOOPLV,   0, 32, 22,   8 },     /* 0 .. 256 */
    { "LEVEL",  NO_DSP,     0, 20, 20,   0 },     /* output level, 5 % steps */
};
static const int16_t speed_q8[9] = { -512, -256, -128, 128, 192, 256, 384, 512, 1024 };
static const char *const speed_name[9] = { "REV 2X", "REV 1X", "REV.5X", "0.5X", "0.75X", "1X", "1.5X", "2X", "4X" };

static int16_t step[S_N];
static uint8_t sel, on, frozen, live, have_knob;
static uint8_t knob_ref;       /* knob reading when the setting was chosen */
static uint32_t dirty;         /* bit = DSP parameter id */

void fx_ui_init(void)
{
    unsigned i;
    for (i = 0; i < S_N; i++) step[i] = par[i].def;
    sel = S_MIX; on = 0; frozen = 0; live = 0; have_knob = 0; knob_ref = 0;
    dirty = 0;
}

void fx_ui_resend_all(void) { dirty = (1u << D_N) - 1u; }

void fx_ui_turn(int detents)
{
    int n;
    if (!detents) return;
    n = ((int)sel + detents) % S_N;
    sel = (uint8_t)(n < 0 ? n + S_N : n);
    live = 0;                  /* the newly chosen setting keeps its value until the knob is turned */
    have_knob = 0;
}

void fx_ui_foot_press(void)
{
    frozen = !frozen;
    dirty |= 1u << D_FREEZE;
    if (frozen && !on) { on = 1; dirty |= 1u << D_MODE; }
}

void fx_ui_para_push(void)
{
    on = !on;
    dirty |= 1u << D_MODE;
    if (!on && frozen) { frozen = 0; dirty |= 1u << D_FREEZE; }
}

#define KNOB_MOVE 6            /* of 255: how far the knob has to turn before it takes the setting over */
void fx_ui_knob(unsigned knob)
{
    int v, d;
    if (knob > 255u) knob = 255u;
    if (!have_knob) { have_knob = 1; knob_ref = (uint8_t)knob; return; }
    if (!live) {
        d = (int)knob - (int)knob_ref;
        if (d > -KNOB_MOVE && d < KNOB_MOVE) return;
        live = 1;
    }
    v = par[sel].lo + (int)((knob * (unsigned)(par[sel].hi - par[sel].lo) + 127u) / 255u);
    if (v != step[sel]) {
        step[sel] = (int16_t)v;
        if (par[sel].dsp != NO_DSP) dirty |= 1u << par[sel].dsp;
    }
}

unsigned fx_ui_live(void) { return live; }

static uint32_t dsp_value(unsigned id)
{
    unsigned i;
    if (id == D_MODE) return on;
    if (id == D_FREEZE) return frozen;
    for (i = 0; i < S_N; i++) {
        if (par[i].dsp != id) continue;
        if (i == S_SPEED || i == S_LSPEED) return (uint32_t)(int32_t)speed_q8[step[i]];
        return (uint32_t)step[i] * par[i].scale;
    }
    return 0;
}

int fx_ui_take(uint32_t *id, uint32_t *value)
{
    /* the loop and the on switch go last, so they act with the other settings already in place */
    static const uint8_t order[D_N] = { D_DELAY, D_SIZE, D_SPEED, D_SPRAY, D_FEEDBACK, D_MIX, D_CLOCK, D_LOOP,
                                        D_CHAOS, D_LOOPLV, D_LSPEED, D_FREEZE, D_MODE };
    unsigned i;
    for (i = 0; i < D_N; i++) {
        unsigned k = order[i];
        if (dirty & (1u << k)) {
            dirty &= ~(1u << k);
            *id = k; *value = dsp_value(k);
            return 1;
        }
    }
    return 0;
}

unsigned fx_ui_on(void) { return on; }
unsigned fx_ui_frozen(void) { return frozen; }
unsigned fx_ui_level(void) { return ((unsigned)step[S_LEVEL] * 255u + 10u) / 20u; }

static char *num(char *p, unsigned v)
{
    char t[6]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

static char *str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

void fx_ui_value(char *v)
{
    char *p = v;
    unsigned st = (unsigned)step[sel], ck = (unsigned)step[S_CLOCK];

    switch (sel) {
    case S_SPEED: case S_LSPEED: p = str(p, speed_name[st]); break;
    case S_DELAY: case S_SPRAY: case S_LOOP: p = num(p, st * 10 * ck); *p++ = 'M'; *p++ = 'S'; break;   /* real time */
    case S_GRAIN: p = num(p, st * 5 * ck); *p++ = 'M'; *p++ = 'S'; break;
    case S_FEEDBACK: p = num(p, (st * 1000 + 128) / 256); *p++ = '%'; break;
    case S_MIX: case S_CHAOS: case S_LOOPLV: p = num(p, (st * 100 + 16) / 32); *p++ = '%'; break;
    case S_LEVEL: p = num(p, st * 5); *p++ = '%'; break;
    default: *p++ = '/'; p = num(p, st); break;
    }
    *p = 0;
}

const char *fx_ui_name(void) { return par[sel].name; }
unsigned fx_ui_sel(void) { return sel; }
unsigned fx_ui_frac(void)
{
    return ((unsigned)(step[sel] - par[sel].lo) * 255u) / (unsigned)(par[sel].hi - par[sel].lo);
}

void fx_ui_text(char *line)
{
    char v[FX_COLS + 1];
    unsigned i, n, vl = 0;

    fx_ui_value(v);
    while (v[vl]) vl++;
    for (n = 0; par[sel].name[n]; n++) line[n] = par[sel].name[n];
    for (i = n; i < FX_COLS - vl; i++) line[i] = ' ';     /* value right-aligned */
    for (i = 0; i < vl; i++) line[FX_COLS - vl + i] = v[i];
    line[FX_COLS] = 0;
}

void fx_ui_status(char *line)
{
    char *p = line;
    unsigned pct = ((unsigned)step[S_MIX] * 100u + 16u) / 32u;
    p = str(p, "MIX");
    if (pct < 100) *p++ = ' ';
    if (pct < 10) *p++ = ' ';
    p = num(p, pct);
    p = str(p, on ? " ON  " : " OFF ");
    p = str(p, frozen ? "FR" : "  ");
    *p = 0;
}
