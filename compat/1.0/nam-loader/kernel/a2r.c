/* NAM A2 Lite, 32-bit residual stream. See a2r.h. Builds for the host (portable maths), the control processor
 * (NDS32 DSP extension: rounding multiplies, bit-exact with the host) and the DSP (Cortex-M4: SMLAWB/T truncate,
 * so a2r_bind adds the mean truncation back to the biases; close to, not bit-exact with, the others).
 * A chip may run only some of the layers (a2r_bind_range): the residual stream then comes in from / goes out to
 * the other chip, and each chip's head output is its own layers' share (the head is linear: the shares add up). */
#include "a2r.h"

#define C A2R_C
#define B A2R_BLOCK

#if defined(__NDS32_EXT_DSP__)
#include <nds32_intrinsic.h>
#define KMB(acc, a, b) __nds32__kmmawb_u((acc), (a), (b))     /* acc + round(a * b.lo / 2^16), saturating */
#define KMT(acc, a, b) __nds32__kmmawt_u((acc), (a), (b))     /* acc + round(a * b.hi / 2^16) */
#define SMMWB(a, b) __nds32__smmwb((a), (b))                  /* (a * b.lo) >> 16 */
#define KMM(acc, a, b) __nds32__kmmac_u((acc), (a), (b))       /* acc + round(a * b / 2^32), saturating */
#elif defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
#define A2R_TRUNC 1                                            /* multiplies truncate instead of rounding */
#define KMB(acc, a, b) __smlawb((a), (int32_t)(b), (acc))      /* acc + (a * b.lo) >> 16 */
#define KMT(acc, a, b) __smlawt((a), (int32_t)(b), (acc))      /* acc + (a * b.hi) >> 16 */
#define SMMWB(a, b) __smlawb((a), (int32_t)(b), 0)              /* (a * b.lo) >> 16 */
static inline int32_t KMM(int32_t acc, int32_t a, int32_t b)   /* acc + round(a * b / 2^32) */
{
    int32_t r; __asm__("smmlar %0, %1, %2, %3" : "=r"(r) : "r"(a), "r"(b), "r"(acc)); return r;
}
#elif defined(A2R_EMU_TRUNC)                                  /* host: the DSP's truncating maths, for checks */
#define A2R_TRUNC 1
static inline int32_t KMB(int32_t acc, int32_t a, uint32_t b) { return acc + (int32_t)(((int64_t)a * (int16_t)b) >> 16); }
static inline int32_t KMT(int32_t acc, int32_t a, uint32_t b) { return acc + (int32_t)(((int64_t)a * (int16_t)(b >> 16)) >> 16); }
static inline int32_t SMMWB(int32_t a, uint32_t b) { return (int32_t)(((int64_t)a * (int16_t)b) >> 16); }
static inline int32_t KMM(int32_t acc, int32_t a, int32_t b) { return acc + (int32_t)(((int64_t)a * b + 0x80000000LL) >> 32); }
#else
static inline int32_t sat32(int64_t v) { return v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (int32_t)0x80000000 : (int32_t)v; }
static inline int32_t KMB(int32_t acc, int32_t a, uint32_t b) { return sat32((int64_t)acc + (((int64_t)a * (int16_t)b + 0x8000) >> 16)); }
static inline int32_t KMT(int32_t acc, int32_t a, uint32_t b) { return sat32((int64_t)acc + (((int64_t)a * (int16_t)(b >> 16) + 0x8000) >> 16)); }
static inline int32_t SMMWB(int32_t a, uint32_t b) { return (int32_t)(((int64_t)a * (int16_t)b) >> 16); }
static inline int32_t KMM(int32_t acc, int32_t a, int32_t b) { return sat32((int64_t)acc + (((int64_t)a * b + 0x80000000LL) >> 32)); }
#endif
static void wcopy(int32_t *d, const int32_t *s, int n)       /* d <= s if overlapping (forward) */
{
#if defined(__NDS32_EXT_DSP__)
    /* 8 words per load-multiple / store-multiple (the history rewinds copy up to ~3,600 words; 2 instructions a word
     * before: 2.2..8.9 % of a block went to them). Checked in the simulator: kernel/test/lmw_test.c */
    for (; n >= 8; n -= 8)
        __asm__ volatile("lmw.bim $r16, [%1], $r23, 0x0\n\tsmw.bim $r16, [%0], $r23, 0x0"
                         : "+r"(d), "+r"(s) : : "$r16", "$r17", "$r18", "$r19", "$r20", "$r21", "$r22", "$r23", "memory");
#endif
    while (n-- > 0) *d++ = *s++;
}
static void wzero(int32_t *d, int n) { while (n-- > 0) *d++ = 0; }
#define PACK(lo, hi) ((uint32_t)(uint16_t)(lo) | ((uint32_t)(uint16_t)(hi) << 16))

static int hist_cap(const a2r_layer_def_t *d) { return (d->K - 1) * d->D + 4 * B; }   /* rewind every 4th block */

int a2r_mem_bytes_range(const a2r_model_t *m, int l0, int l1)
{
    int words = 0;
    for (int l = l0; l < l1; l++) words += hist_cap(&m->layers[l]) * C;
    return words * 4;
}
int a2r_mem_bytes(const a2r_model_t *m) { return a2r_mem_bytes_range(m, 0, m->nl); }

static void pack9(uint32_t *dst, const int16_t *w9)
{
    for (int j = 0; j < 5; j++) dst[j] = PACK(w9[2 * j], j < 4 ? w9[2 * j + 1] : 0);
}

int a2r_bind_range(a2r_state_t *s, const a2r_model_t *m, int l0, int l1, void *mem, int bytes)
{
    if (m->nl > A2R_MAX_LAYERS || m->kh > A2R_MAX_KH || l0 < 0 || l1 > m->nl || l0 >= l1 ||
        bytes < a2r_mem_bytes_range(m, l0, l1)) return -1;
    wzero((int32_t *)s, (int)(sizeof *s / 4));
    s->m = m; s->l0 = l0; s->l1 = l1;
    int32_t *p = (int32_t *)mem;
    for (int l = l0; l < l1; l++) {
        const a2r_layer_def_t *d = &m->layers[l];
        a2r_layer_rt_t *L = &s->L[l];
        if (d->K > 15) return -3;
        L->K = d->K; L->D = d->D; L->len = hist_cap(d);
        L->sh_head = (int8_t)(d->ez - m->eh); L->sh_r = (int8_t)(d->er - m->X);
        if (L->sh_head < 0 || L->sh_head > 30 || L->sh_r > 30 || L->sh_r < -30) return -2;
        for (int o = 0; o < C; o++) { L->b[o] = d->b[o]; L->mix[o] = d->mix[o]; L->l1b[o] = d->l1b[o]; }
#ifdef A2R_TRUNC
        for (int o = 0; o < C; o++) { L->b[o] += (3 * d->K + 1) / 2; L->l1b[o] += 1; }   /* mean truncation, -0.5 per multiply */
#endif
        pack9(L->l1p, d->l1w);
        for (int k = 0; k < d->K; k++) {
            int16_t w9[9];
            for (int o = 0; o < C; o++) for (int i = 0; i < C; i++) w9[o * C + i] = d->w[(o * C + i) * d->K + k];
            pack9(&L->wpk[5 * k], w9);
        }
        L->ring = p; p += L->len * C;
    }
    float sc = 1.0f;
    for (int e = m->eh + m->ehw; e > 0; e--) sc *= 0.5f;
    s->out_k = m->hs * sc * 2147483648.0f;
    s->out_b = m->hs * m->hb * 2147483648.0f;
    a2r_reset(s);
    return 0;
}
int a2r_bind(a2r_state_t *s, const a2r_model_t *m, void *mem, int bytes) { return a2r_bind_range(s, m, 0, m->nl, mem, bytes); }

void a2r_reset(a2r_state_t *s)
{
    /* The history before pos is silence. Each layer rewinds (copies its history back to the start) every 4th block;
     * its starting point sets in which of the 4 blocks. The biggest go first, each into the block with the least
     * copying so far, so no block copies two big histories at once (2026-11-16: 2.2..8.9 % of a block before). */
    int load[4] = { 0, 0, 0, 0 }, done[A2R_MAX_LAYERS] = { 0 };
    for (int n = s->l0; n < s->l1; n++) {
        int best = -1, phase = 0;
        for (int l = s->l0; l < s->l1; l++)
            if (!done[l] && (best < 0 || (s->L[l].K - 1) * s->L[l].D > (s->L[best].K - 1) * s->L[best].D)) best = l;
        for (int q = 1; q < 4; q++) if (load[q] < load[phase]) phase = q;
        done[best] = 1;
        load[phase] += (s->L[best].K - 1) * s->L[best].D;
        wzero(s->L[best].ring, s->L[best].len * C);
        s->L[best].pos = (s->L[best].K - 1) * s->L[best].D + phase * B;
    }
    wzero(s->hbuf, (int)(sizeof s->hbuf / 4));
}

/* z[t][o] = b + mix u + sum_k W_k x(t - (K-1-k) D), two samples per pass; p = row of tap 0 for t = 0.
 * (Four per pass loads each weight word half as often but runs out of registers with this compiler: slower.) */
static void conv32(const int32_t *p, const uint32_t *wpk, const a2r_layer_rt_t *L, const int32_t *u, int32_t *z, int K, int D)
{
    const int step = C * D;
    for (int t = 0; t < B; t += 2, p += 2 * C, z += 2 * C) {
        const int32_t u0 = u[t], u1 = u[t + 1];
        int32_t a0 = KMM(L->b[0], L->mix[0], u0), a1 = KMM(L->b[1], L->mix[1], u0), a2 = KMM(L->b[2], L->mix[2], u0);
        int32_t c0 = KMM(L->b[0], L->mix[0], u1), c1 = KMM(L->b[1], L->mix[1], u1), c2 = KMM(L->b[2], L->mix[2], u1);
        const int32_t *h = p;
        const uint32_t *w = wpk;
        for (int k = 0; k < K; k++, h += step, w += 5) {
            const int32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4], h5 = h[5];
            const uint32_t w0 = w[0], w1 = w[1], w2 = w[2], w3 = w[3], w4 = w[4];
            a0 = KMB(a0, h0, w0); a0 = KMT(a0, h1, w0); a0 = KMB(a0, h2, w1);
            a1 = KMT(a1, h0, w1); a1 = KMB(a1, h1, w2); a1 = KMT(a1, h2, w2);
            a2 = KMB(a2, h0, w3); a2 = KMT(a2, h1, w3); a2 = KMB(a2, h2, w4);
            c0 = KMB(c0, h3, w0); c0 = KMT(c0, h4, w0); c0 = KMB(c0, h5, w1);
            c1 = KMT(c1, h3, w1); c1 = KMB(c1, h4, w2); c1 = KMT(c1, h5, w2);
            c2 = KMB(c2, h3, w3); c2 = KMT(c2, h4, w3); c2 = KMB(c2, h5, w4);
        }
        z[0] = a0; z[1] = a1; z[2] = a2; z[3] = c0; z[4] = c1; z[5] = c2;
    }
}

/* LeakyReLU, head sum, 1x1 + residual: x_out = x_in + r (rows of 3), straight into the next layer's history;
 * x_out = 0 for the last layer (its residual is not used) */
static void post32(const int32_t *z, const a2r_layer_rt_t *L, uint32_t slope, const int32_t *x, int32_t *xo, int32_t *head)
{
    const int sh_head = L->sh_head, sh_r = L->sh_r;
    const int32_t hh = (1 << sh_head) >> 1, hr = sh_r > 0 ? (1 << sh_r) >> 1 : 0;
    const uint32_t p0 = L->l1p[0], p1 = L->l1p[1], p2 = L->l1p[2], p3 = L->l1p[3], p4 = L->l1p[4];
    const int32_t lb0 = L->l1b[0], lb1 = L->l1b[1], lb2 = L->l1b[2];
    for (int t = 0; t < B; t++, z += C, x += C, head += C) {
        int32_t a0 = z[0], a1 = z[1], a2 = z[2];
        if (a0 < 0) a0 = SMMWB(a0, slope);                     /* (branchless was slower on the pedal: +2 % of a block) */
        if (a1 < 0) a1 = SMMWB(a1, slope);
        if (a2 < 0) a2 = SMMWB(a2, slope);
        head[0] += (a0 + hh) >> sh_head; head[1] += (a1 + hh) >> sh_head; head[2] += (a2 + hh) >> sh_head;
        if (!xo) continue;
        int32_t r0 = KMB(lb0, a0, p0); r0 = KMT(r0, a1, p0); r0 = KMB(r0, a2, p1);
        int32_t r1 = KMT(lb1, a0, p1); r1 = KMB(r1, a1, p2); r1 = KMT(r1, a2, p2);
        int32_t r2 = KMB(lb2, a0, p3); r2 = KMT(r2, a1, p3); r2 = KMB(r2, a2, p4);
        if (sh_r >= 0) { xo[0] = x[0] + ((r0 + hr) >> sh_r); xo[1] = x[1] + ((r1 + hr) >> sh_r); xo[2] = x[2] + ((r2 + hr) >> sh_r); }
        else { xo[0] = x[0] + (r0 << -sh_r); xo[1] = x[1] + (r1 << -sh_r); xo[2] = x[2] + (r2 << -sh_r); }
        xo += C;
    }
}

static int32_t g_z[B * C];                   /* static: this runs in an interrupt */
#ifdef A2R_PROF                              /* cycles per stage of the last a2r_run: history, conv, post, output */
volatile uint32_t a2r_prof[4];
#define PT(i, t) do { uint32_t n_ = A2R_PROF(); a2r_prof[i] += n_ - (t); (t) = n_; } while (0)
#else
#define PT(i, t) ((void)0)
#endif

static int32_t *room(a2r_layer_rt_t *L)      /* this layer's history: make room for a block, return its rows */
{
    const int back = (L->K - 1) * L->D;
    if (L->pos + B > L->len) {
        wcopy(L->ring, L->ring + C * (L->pos - back), C * back);
        L->pos = back;
    }
    return L->ring + C * L->pos;
}

/* One block through layers l0..l1-1. u: the guitar input (Q31; every layer mixes it in). xin: the residual stream
 * into layer l0 (rows of 3), or 0 when l0 = 0 (made from u). xout: the stream out of layer l1-1, for the other chip
 * (unused when l1 is the last layer). out: this chip's share of the output (Q31), with the head bias if bias. */
void a2r_run(a2r_state_t *s, const int32_t *u, const int32_t *xin, int32_t *xout, int32_t *out, int bias)
{
    const a2r_model_t *m = s->m;
    const int kh = m->kh, nl = m->nl, l0 = s->l0, l1 = s->l1;
    int32_t *head = &s->hbuf[(kh - 1) * C];
#ifdef A2R_PROF
    uint32_t pt = A2R_PROF();
    a2r_prof[0] = a2r_prof[1] = a2r_prof[2] = a2r_prof[3] = 0;
#endif
    wzero(head, B * C);
    int32_t *x = room(&s->L[l0]);
    if (xin) wcopy(x, xin, B * C);
    else for (int t = 0; t < B; t++) {
        const int32_t v = u[t];
        x[C * t] = KMM(0, m->rech[0], v); x[C * t + 1] = KMM(0, m->rech[1], v); x[C * t + 2] = KMM(0, m->rech[2], v);
    }
    for (int l = l0; l < l1; l++) {
        a2r_layer_rt_t *L = &s->L[l];
        int32_t *xo = l + 1 < l1 ? room(&s->L[l + 1]) : l + 1 < nl ? xout : 0;
        PT(0, pt);
        conv32(x - C * (L->K - 1) * L->D, L->wpk, L, u, g_z, L->K, L->D);
        PT(1, pt);
        post32(g_z, L, (uint32_t)m->slope_q16, x, xo, head);
        PT(2, pt);
        L->pos += B;
        x = xo;
    }
    const int16_t *hw = m->hw;
    const float ob = bias ? s->out_b : 0.0f;
    for (int t = 0; t < B; t++) {
        const int32_t *h = &s->hbuf[t * C];
        int64_t y = 0;
        for (int j = 0; j < kh; j++)
            y += (int64_t)hw[j] * h[j * C] + (int64_t)hw[kh + j] * h[j * C + 1] + (int64_t)hw[2 * kh + j] * h[j * C + 2];
        /* int64 -> float in two hardware conversions (the library's int64 one is slow): |y| < 2^52 */
        float v = ((float)(int32_t)(y >> 21) * 2097152.0f + (float)(int32_t)(y & 0x1FFFFF)) * s->out_k + ob;
        out[t] = v >= 2147483520.0f ? 2147483647 : v <= -2147483648.0f ? (int32_t)0x80000000 : (int32_t)v;
    }
    wcopy(s->hbuf, &s->hbuf[B * C], (kh - 1) * C);
    PT(3, pt);
}

void a2r_process(a2r_state_t *s, const int32_t *in, int32_t *out) { a2r_run(s, in, 0, 0, out, 1); }

/* NAM's pad_start: the response to silence. Only for a chip that runs from layer 0 (it has the whole history). */
void a2r_prime(a2r_state_t *s)
{
    static int32_t z[B], o[B], xo[B * C];
    int rf = s->m->kh;
    for (int l = 0; l < s->m->nl; l++) rf += (s->m->layers[l].K - 1) * s->m->layers[l].D;
    for (int left = rf - 1; left > 0; left -= B) a2r_run(s, z, 0, xo, o, 1);
}
