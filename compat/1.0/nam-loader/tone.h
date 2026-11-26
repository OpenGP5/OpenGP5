/* NAM Loader's tone section, after the model and the cab IR: a noise gate and a 3-band EQ, one setting per tone.
 * Runs on the control processor in the audio link's interrupt, in fixed point (its integer DSP instructions are
 * about one per cycle; its float unit about five): about 1 % of a block with everything on, nothing when off / flat.
 *
 *   gate    detects on the guitar input and turns the model's output down before the cab IR and the EQ (as the NAM
 *           plugin: listening to the input, gating the output; gating the input instead would change how fading
 *           notes distort), so the cab's ring-out and the EQ smooth its movement: opens at the threshold, closes 6 dB below it after a 50 ms hold, opens in
 *           about 0.3 ms and fades out over about 100 ms. Setting 0 = off, 1..61 = threshold -90 .. -30 dBFS.
 *   EQ      bass (low shelf, 120 Hz), mid (peak, 750 Hz, Q 0.7), treble (high shelf, 3 kHz): RBJ biquads, coefficients
 *           Q28, direct form I with 64-bit sums. Settings 0..24 = -12 .. +12 dB (12 = flat: the band is skipped).
 *
 * The settings change from the main loop (tone_set: computes the coefficients in float, then switches banks with
 * one word, so the interrupt never sees half of them). Plain C: also builds on the computer (test/tone_test.c). */
#ifndef NAM_TONE_H
#define NAM_TONE_H
#include <stdint.h>

#define TONE_GATE_MAX  61u
#define TONE_EQ_MAX    24u
#define TONE_EQ_FLAT   12u
#define TONE_FS        48000.0f
#define TONE_HOLD      2400u          /* 50 ms */
typedef struct { uint8_t gate, bass, mid, treble; } tone_set_t;
#define TONE_DEFAULT ((tone_set_t){ 0, TONE_EQ_FLAT, TONE_EQ_FLAT, TONE_EQ_FLAT })

typedef struct { int32_t b0, b1, b2, a1, a2; int on; } tone_bq_t;
typedef struct {
    tone_bq_t bq[3];
    uint32_t thr_open, thr_close;     /* gate thresholds (Q31 amplitude); 0 = gate off */
} tone_bank_t;
typedef struct {
    tone_bank_t bank[2];
    volatile uint32_t active;         /* the bank the interrupt uses */
    int32_t z[3][4];                  /* each band's x1, x2, y1, y2 */
    uint32_t env, hold, open;         /* gate detector */
    uint32_t g;                       /* gate gain, Q30 */
} tone_t;

static const float tone_freq[3] = { 120.0f, 750.0f, 3000.0f };

/* one band's coefficients (RBJ audio EQ cookbook): kind 0 low shelf, 1 peak (Q 0.7), 2 high shelf; slope 1 */
static void tone_coefs(tone_bq_t *q, int kind, float db)
{
    float A = expf(db * 0.0575646273f);                       /* 10^(db/40) */
    float w = 6.28318531f * tone_freq[kind] / TONE_FS, c = cosf(w), s = sinf(w);
    float b0, b1, b2, a0, a1, a2;
    q->on = db != 0.0f;
    if (kind == 1) {
        float al = s / (2.0f * 0.7f);
        b0 = 1.0f + al * A; b1 = -2.0f * c; b2 = 1.0f - al * A;
        a0 = 1.0f + al / A; a1 = -2.0f * c; a2 = 1.0f - al / A;
    } else {
        float al = s * 0.70710678f, r = 2.0f * sqrtf(A) * al, ap = A + 1.0f, am = A - 1.0f;
        if (kind == 0) {
            b0 = A * (ap - am * c + r); b1 = 2.0f * A * (am - ap * c); b2 = A * (ap - am * c - r);
            a0 = ap + am * c + r;       a1 = -2.0f * (am + ap * c);    a2 = ap + am * c - r;
        } else {
            b0 = A * (ap + am * c + r); b1 = -2.0f * A * (am + ap * c); b2 = A * (ap + am * c - r);
            a0 = ap - am * c + r;       a1 = 2.0f * (am - ap * c);      a2 = ap - am * c - r;
        }
    }
    {
        float k = 268435456.0f / a0;                          /* Q28 */
        q->b0 = (int32_t)(b0 * k + (b0 < 0 ? -0.5f : 0.5f)); q->b1 = (int32_t)(b1 * k + (b1 < 0 ? -0.5f : 0.5f));
        q->b2 = (int32_t)(b2 * k + (b2 < 0 ? -0.5f : 0.5f)); q->a1 = (int32_t)(a1 * k + (a1 < 0 ? -0.5f : 0.5f));
        q->a2 = (int32_t)(a2 * k + (a2 < 0 ? -0.5f : 0.5f));
    }
}

static void tone_init(tone_t *t)
{
    unsigned i, j;
    for (i = 0; i < 2; i++) { for (j = 0; j < 3; j++) t->bank[i].bq[j].on = 0; t->bank[i].thr_open = t->bank[i].thr_close = 0; }
    for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) t->z[i][j] = 0;
    t->active = 0; t->env = 0; t->hold = 0; t->open = 1; t->g = 1u << 30;
}

/* main loop: new settings (clamped) */
static void tone_set(tone_t *t, tone_set_t s)
{
    tone_bank_t *b = &t->bank[t->active ^ 1u];
    uint8_t v[3] = { s.bass, s.mid, s.treble };
    unsigned i;
    for (i = 0; i < 3; i++) tone_coefs(&b->bq[i], (int)i, (float)((int)(v[i] > TONE_EQ_MAX ? TONE_EQ_MAX : v[i]) - (int)TONE_EQ_FLAT));
    if (s.gate == 0) b->thr_open = b->thr_close = 0;
    else {
        float db = -91.0f + (float)(s.gate > TONE_GATE_MAX ? TONE_GATE_MAX : s.gate);
        b->thr_open = (uint32_t)(2147483648.0f * expf(db * 0.115129255f));
        b->thr_close = b->thr_open >> 1;                       /* 6 dB below */
    }
    t->active ^= 1u;
}

static inline int32_t tone_sat(int64_t v) { return v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (int32_t)0x80000000 : (int32_t)v; }

/* interrupt, after the model: in = the guitar input of this block (detector), y = the model's output (in place) */
static void tone_gate(tone_t *t, const int32_t *in, int32_t *y, unsigned n)
{
    const tone_bank_t *b = &t->bank[t->active];
    unsigned i;
    if (!b->thr_open) { t->g = 1u << 30; t->open = 1; t->hold = 0; return; }
    for (i = 0; i < n; i++) {
        int32_t v = in[i];
        uint32_t a = v < 0 ? (uint32_t)(-(v + 1)) : (uint32_t)v;
        if (a > t->env) t->env = a; else t->env -= t->env >> 9;          /* peak, ~11 ms decay */
        if (t->env > b->thr_open) { t->open = 1; t->hold = TONE_HOLD; }
        else if (t->env < b->thr_close) { if (t->hold) t->hold--; else t->open = 0; }
        if (t->open) t->g += ((1u << 30) - t->g) >> 4;                   /* ~0.3 ms */
        else t->g -= t->g >> 10;                                          /* ~21 ms time constant */
        y[i] = (int32_t)(((int64_t)y[i] * t->g) >> 30);
    }
}

/* interrupt, last (after the cab IR): the EQ on y (in place) */
static void tone_eq(tone_t *t, int32_t *y, unsigned n)
{
    const tone_bank_t *b = &t->bank[t->active];
    unsigned i, k;
    for (k = 0; k < 3; k++) {
        const tone_bq_t *q = &b->bq[k];
        int32_t *z = t->z[k];
        if (!q->on) { z[0] = z[1] = z[2] = z[3] = 0; continue; }
        for (i = 0; i < n; i++) {
            int32_t x = y[i];
            int64_t acc = (int64_t)q->b0 * x + (int64_t)q->b1 * z[0] + (int64_t)q->b2 * z[1]
                        - (int64_t)q->a1 * z[2] - (int64_t)q->a2 * z[3];
            int32_t o = tone_sat((acc + (1LL << 27)) >> 28);
            z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = o;
            y[i] = o;
        }
    }
}

/* both in one go (the host test): gate, then EQ */
static inline void tone_run(tone_t *t, const int32_t *in, int32_t *y, unsigned n) { tone_gate(t, in, y, n); tone_eq(t, y, n); }
#endif
