/*
 * Granular delay + micro-looper, v3. First custom effect for the from-scratch GP-5 DSP firmware.
 * Plain integer C, no libc, no globals: the same file builds into the firmware (-D FX_GRANULAR,
 * included by audio.h) and into the host test (tools/granular_sim.py).
 *
 * Two 16-bit mono recordings, each with its own set of grains:
 *
 *   delay buffer  always recording (input + feedback). Grains start DELAY (+ random 0..SPRAY) behind
 *                 the write head and play at SPEED. This is the granular delay; it keeps working on
 *                 the live input while a loop plays.
 *   loop buffer   records what you hear without the loop (input + delay grains) until FREEZE. Then
 *                 it stops, and LOOP samples of it play as a loop: a head runs through them at
 *                 LSPEED (tape style: half speed = octave down and twice as long, negative =
 *                 backwards) and grains are taken at the head. Grains never jump across the loop
 *                 seam: every grain reads one grain length of audio that ends at the head, so the
 *                 seam is a crossfade into the audio just before the loop. (That also means the
 *                 loop ends one grain length before the moment of the freeze.)
 *                 Unfreeze: the grains finish, recording resumes.
 *
 * A grain's position is its "age": how far behind the buffer's write head it reads (Q8). Writing a
 * sample ages every grain of that buffer by one; playing moves it by the grain's speed.
 * Window: smoothstep of a triangle, so two grains at 50 % overlap sum to exactly 1.
 *
 * CHAOS (0..256) makes each grain a little different: it may jump an octave up or down or play
 * backwards, its length varies by up to +-50 %, some grains are left out, and each gets its own
 * place between the left and right output. At 0 everything is exact and both outputs are equal.
 *
 * CLOCK 1..8: the whole engine runs at 44.1 kHz / CLOCK (longer times, less treble, more grit).
 * All sample counts are in engine samples.
 *
 * Parameters (gr_set), clamped to their range:
 *    0 MODE      0 off (output = input, bit-exact; both buffers keep recording), 1 on
 *    1 DELAY     0 .. delay buffer - 2
 *    2 SIZE      64 .. 8192 samples per grain
 *    3 SPEED     Q8 signed, delay grains: 256 = 1x, 512 = octave up, 128 = down, negative = reverse; |x| 16..1024
 *    4 SPRAY     0 .. delay buffer / 2: random extra distance per grain (both buffers)
 *    5 FEEDBACK  Q8, 0..250
 *    6 MIX       0 dry only .. 128 dry and effect both at full level .. 256 effect only
 *    7 CLOCK     1..8
 *    8 LOOP      256 .. loop buffer - 4096
 *    9 FREEZE    0 / 1
 *   10 CHAOS     0..256
 *   11 LOOPLV    Q8 loop level, 0..256
 *   12 LSPEED    Q8 signed loop speed, as SPEED
 */
#ifndef GRANULAR_H
#define GRANULAR_H
#include <stdint.h>

#define GR_NG 5
enum { GR_MODE, GR_DELAY, GR_SIZE, GR_SPEED, GR_SPRAY, GR_FEEDBACK, GR_MIX, GR_CLOCK, GR_LOOP,
       GR_FREEZE, GR_CHAOS, GR_LOOPLV, GR_LSPEED, GR_NP };

typedef struct {
    uint32_t on, n, size;     /* samples played / grain length */
    uint32_t ph, inc;         /* window phase, Q24 (0 .. 1<<24 over the grain), and its step */
    int32_t age, speed;       /* Q8 */
    int32_t gl, gr;           /* output gains, Q8 */
} gr_grain_t;

typedef struct {
    int16_t *buf;
    uint32_t len, w, since;
    gr_grain_t g[GR_NG];
} gr_eng_t;

typedef struct {
    gr_eng_t d, l;            /* delay, loop */
    uint32_t p[GR_NP];
    uint32_t frozen;
    int32_t head;             /* loop head, Q8, 0 .. LOOP<<8 */
    uint32_t rng;
    uint32_t ck;              /* clock phase 0..CLOCK-1 */
    int32_t acc;              /* input sum for the current engine sample */
    int32_t prev[2], cur[2];  /* effect output of the last two engine samples, left / right */
    uint32_t spawned, dropped; /* grains started / not started because all slots were busy */
} gr_t;

static inline int32_t gr_sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static inline uint32_t gr_rand(gr_t *s) { s->rng = s->rng * 1664525u + 1013904223u; return s->rng >> 8; }

static void gr_set(gr_t *s, uint32_t id, uint32_t v)
{
    int32_t sv = (int32_t)v;
    switch (id) {
    case GR_MODE: case GR_FREEZE: v = v ? 1u : 0u; break;
    case GR_DELAY: if (v > s->d.len - 2) v = s->d.len - 2; break;
    case GR_SIZE: if (v < 64) v = 64; if (v > 8192) v = 8192; break;
    case GR_SPEED: case GR_LSPEED:
        if (sv > 1024) sv = 1024;
        if (sv < -1024) sv = -1024;
        if (sv > -16 && sv < 16) sv = sv < 0 ? -16 : 16;
        v = (uint32_t)sv;
        break;
    case GR_SPRAY: if (v > s->d.len / 2) v = s->d.len / 2; break;
    case GR_FEEDBACK: if (v > 250) v = 250; break;
    case GR_MIX: case GR_CHAOS: case GR_LOOPLV: if (v > 256) v = 256; break;
    case GR_CLOCK: if (v < 1) v = 1; if (v > 8) v = 8; break;
    case GR_LOOP: if (v < 256) v = 256; if (v > s->l.len - 4096) v = s->l.len - 4096; break;
    default: return;
    }
    if (id == GR_FREEZE && v != s->frozen) {
        s->frozen = v;
        if (v) {              /* the loop starts at its first sample (its last one when it runs backwards) */
            s->head = (int32_t)s->p[GR_LSPEED] < 0 ? (int32_t)((s->p[GR_LOOP] - 1) << 8) : 0;
            s->l.since = 0x7FFFFFFFu;
        }
    }
    s->p[id] = v;
    if (id == GR_LOOP && s->head >= (int32_t)(v << 8)) s->head = 0;
    if (id == GR_CLOCK) { s->ck = 0; s->acc = 0; }
}

static void gr_init(gr_t *s, int16_t *dbuf, uint32_t dlen, int16_t *lbuf, uint32_t llen)
{
    static const int32_t def[GR_NP] = {0, 13230, 4410, 256, 1764, 110, 128, 1, 22050, 0, 64, 180, 256};
    uint32_t i;
    for (i = 0; i < dlen; i++) dbuf[i] = 0;
    for (i = 0; i < llen; i++) lbuf[i] = 0;
    s->d.buf = dbuf; s->d.len = dlen; s->d.w = 0; s->d.since = 0;
    s->l.buf = lbuf; s->l.len = llen; s->l.w = 0; s->l.since = 0;
    for (i = 0; i < GR_NG; i++) { s->d.g[i].on = 0; s->l.g[i].on = 0; }
    s->frozen = 0; s->head = 0; s->rng = 0x13579BDFu; s->ck = 0; s->acc = 0;
    s->prev[0] = s->prev[1] = s->cur[0] = s->cur[1] = 0;
    s->spawned = 0; s->dropped = 0;
    for (i = 0; i < GR_NP; i++) s->p[i] = (uint32_t)def[i];
    for (i = 0; i < GR_NP; i++) gr_set(s, i, (uint32_t)def[i]);   /* clamp to these buffer sizes */
}

/* Start a grain in e. base = age (samples) of the point the grain is taken from; live = the buffer is
 * being written (ages grow by one per sample). The grain is placed so that it never reads newer than
 * the newest sample or older than the oldest one. */
static void gr_spawn(gr_t *s, gr_eng_t *e, uint32_t base, int32_t speed, int live)
{
    uint32_t chaos = s->p[GR_CHAOS], size = s->p[GR_SIZE], i, r;
    int32_t gl = 256, gr = 256, travel, lo, hi, a0;
    gr_grain_t *g = 0;

    if (chaos) {
        r = gr_rand(s);
        if ((r & 0x3FFu) < chaos) return;                          /* up to 25 %: leave this grain out */
        r = gr_rand(s);
        if ((r & 0x1FFu) < chaos) {                                /* up to 50 %: another pitch */
            uint32_t k = (r >> 9) % 10u;
            if (k < 4) speed *= 2; else if (k < 7) speed /= 2; else speed = -speed;
            if (speed > 1024) speed = 1024;
            if (speed < -1024) speed = -1024;
            if (speed > -16 && speed < 16) speed = speed < 0 ? -16 : 16;
        }
        r = gr_rand(s);                                            /* length x (1 +- chaos/512) */
        size = (size * (uint32_t)(512 + (((int32_t)(r & 0x1FFu) - 256) * (int32_t)chaos) / 256)) >> 9;
        if (size < 32) size = 32;
        r = gr_rand(s);                                            /* place between left and right */
        i = (uint32_t)(128 + (((int32_t)(r & 0xFFu) - 128) * (int32_t)chaos) / 256);
        gl = i >= 128 ? (int32_t)(2u * (256u - i)) : 256;
        gr = i <= 128 ? (int32_t)(2u * i) : 256;
    }
    for (i = 0; i < GR_NG; i++) if (!e->g[i].on) { g = &e->g[i]; break; }
    if (!g) { s->dropped++; return; }

    /* Over the grain its age changes by travel: +1 per sample if the buffer is being written, minus the
     * playing speed. Keep age inside 0 .. len-2 from start to end; shorten the grain if it cannot fit. */
    for (;;) {
        travel = (int32_t)((size * (uint32_t)((live ? 256 : 0) - speed + 1024)) >> 8) - (int32_t)(size * 4u);
        if (travel < 0 ? (uint32_t)-travel <= e->len / 2 : (uint32_t)travel <= e->len / 2) break;
        size >>= 1;
    }
    lo = travel < 0 ? -travel : 0;                 /* smallest allowed start age */
    hi = (int32_t)e->len - 2 - (travel > 0 ? travel : 0);
    a0 = (int32_t)base + (travel < 0 ? -travel : 0);   /* a grain that moves towards the head ends at base */
    if (a0 < lo) a0 = lo;
    if (a0 > hi) a0 = hi;

    g->age = a0 << 8;
    g->speed = speed;
    g->size = size;
    g->n = 0;
    g->ph = 0;
    g->inc = (1u << 24) / size;
    g->gl = gl; g->gr = gr;
    g->on = 1;
    s->spawned++;
}

/* Sum e's grains for one engine sample into out[0] (left), out[1] (right), out[2] (unplaced sum). */
static void gr_run(gr_eng_t *e, int32_t *out)
{
    for (uint32_t i = 0; i < GR_NG; i++) {
        gr_grain_t *g = &e->g[i];
        if (!g->on) continue;
        int32_t age = g->age;
        if (age < 0) age = 0;
        uint32_t idx = (uint32_t)age >> 8, fr = (uint32_t)age & 255u;
        if (idx > e->len - 2) idx = e->len - 2;
        uint32_t i0 = e->w + e->len - 1u - idx;    /* newest sample is at w-1 */
        if (i0 >= e->len) i0 -= e->len;
        uint32_t i1 = i0 ? i0 - 1u : e->len - 1u;  /* one sample older */
        int32_t a = e->buf[i0], b = e->buf[i1];
        int32_t smp = a + (((b - a) * (int32_t)fr) >> 8);
        uint32_t x = g->ph >> 9;                                   /* 0..32767 */
        uint32_t t = x < 16384u ? 2u * x : 65536u - 2u * x;        /* triangle, Q15 */
        uint32_t win = (((t * t) >> 15) * (98304u - 2u * t)) >> 15; /* smoothstep, Q15 */
        smp = (smp * (int32_t)win) >> 15;
        out[0] += (smp * g->gl) >> 8;
        out[1] += (smp * g->gr) >> 8;
        out[2] += smp;
        g->age -= g->speed;
        g->ph += g->inc;
        if (++g->n >= g->size) g->on = 0;
    }
}

static void gr_write(gr_eng_t *e, int32_t v)
{
    e->buf[e->w] = (int16_t)gr_sat16(v);
    if (++e->w >= e->len) e->w = 0;
    for (uint32_t i = 0; i < GR_NG; i++) if (e->g[i].on) e->g[i].age += 256;
}

/* One engine sample: in = input (16-bit range); wet[0], wet[1] = effect output left / right. */
static void gr_tick(gr_t *s, int32_t in, int32_t *wet)
{
    int32_t dly[3] = {0, 0, 0}, lp[3] = {0, 0, 0};
    uint32_t half = s->p[GR_SIZE] >> 1, spray = s->p[GR_SPRAY];

    if (s->p[GR_MODE]) {
        if (s->d.since >= half) {
            s->d.since = 0;
            gr_spawn(s, &s->d, s->p[GR_DELAY] + (spray ? gr_rand(s) % (spray + 1u) : 0u), (int32_t)s->p[GR_SPEED], 1);
        }
        s->d.since++;
        gr_run(&s->d, dly);
        if (s->frozen) {
            int32_t ls = (int32_t)s->p[GR_LSPEED], lim = (int32_t)(s->p[GR_LOOP] << 8);
            if (s->l.since >= half) {
                /* the head is head>>8 samples into the loop; the loop's last sample has age 0 */
                uint32_t base = s->p[GR_LOOP] - 1u - ((uint32_t)s->head >> 8);
                s->l.since = 0;
                gr_spawn(s, &s->l, base + (spray ? gr_rand(s) % (spray + 1u) : 0u), ls, 0);
            }
            s->l.since++;
            s->head += ls;
            if (s->head >= lim) s->head -= lim;
            if (s->head < 0) s->head += lim;
        }
        gr_run(&s->l, lp);                         /* also after unfreeze, until the last grains end */
    }
    gr_write(&s->d, in + ((dly[2] * (int32_t)s->p[GR_FEEDBACK]) >> 8));
    if (!s->frozen) gr_write(&s->l, in + dly[2]);
    wet[0] = gr_sat16(dly[0] + ((lp[0] * (int32_t)s->p[GR_LOOPLV]) >> 8));
    wet[1] = gr_sat16(dly[1] + ((lp[1] * (int32_t)s->p[GR_LOOPLV]) >> 8));
}

static inline int32_t gr_mix(int32_t dry, int32_t wet, int32_t dg, int32_t wg)
{
    int64_t out = ((int64_t)dry * dg + (int64_t)wet * 65536 * wg) >> 8;
    return out > 2147483647 ? 2147483647 : (out < -2147483647 - 1 ? -2147483647 - 1 : (int32_t)out);
}

/* Process n samples: in[] -> outl[], outr[], all in the pedal's 32-bit format (16-bit value << 16). */
static void gr_block(gr_t *s, const int32_t *in, int32_t *outl, int32_t *outr, uint32_t n)
{
    uint32_t ck = s->p[GR_CLOCK], mix = s->p[GR_MIX], mode = s->p[GR_MODE];
    /* Mix law: dry stays at full level up to the middle, the effect is at full level from the middle up. */
    int32_t dg = mix <= 128u ? 256 : (int32_t)(2u * (256u - mix));
    int32_t wg = mix >= 128u ? 256 : (int32_t)(2u * mix);
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = in[i];
        s->acc += x >> 16;
        if (++s->ck >= ck) {
            s->prev[0] = s->cur[0]; s->prev[1] = s->cur[1];
            gr_tick(s, s->acc / (int32_t)ck, s->cur);
            s->acc = 0;
            s->ck = 0;
        }
        if (!mode) { outl[i] = x; outr[i] = x; continue; }      /* off: sample untouched */
        int32_t wl = s->prev[0] + ((s->cur[0] - s->prev[0]) * (int32_t)s->ck) / (int32_t)ck;
        int32_t wr = s->prev[1] + ((s->cur[1] - s->prev[1]) * (int32_t)s->ck) / (int32_t)ck;
        outl[i] = gr_mix(x, wl, dg, wg);
        outr[i] = gr_mix(x, wr, dg, wg);
    }
}
#endif
