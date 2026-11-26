/* Cab IR convolver in fixed point: uniform partitioned overlap-save, 64-sample blocks, Q31.
 * Why fixed point: on the control processor the FPU takes about 5 cycles per float operation with no overlap
 * (measured: ~50 cycles per complex multiply-add, 9 % of a 48 kHz block for a 256-tap IR). The DSP extension's
 * 32 x 32 multiplies run about one per cycle (the NAM kernel does), so here every multiply is one of those:
 *   kwmmul.u   round(a b / 2^31)           Q31 x Q31 -> Q31 (the FFTs' twiddles)
 *   kmmac.u    acc + round(a b / 2^32)     the products with the IR (and kmmsb.u: acc - ...), saturating
 *   raddw / rsubw   (a + b) / 2, (a - b) / 2 without overflow (every FFT stage halves: nothing can overflow)
 * The same file runs on the computer (portable versions of those, bit-exact) for the tests.
 *
 * Levels: the input goes in shifted right by CABQ_IN (2 bits of room for the complex rotations and the sums over
 * partitions); the forward FFT halves at each of its 7 stages (64-point complex + the real split); the IR spectra
 * are scaled by 2^-eh so that their largest part fits; the products add half of each; the inverse halves at each of
 * its 7 stages and once more in its split. The output comes back with one left shift (cabq_set works it out),
 * saturating: an IR with more gain than 1 can clip, as it would anywhere.
 * Memory, kept small for the cache (the model's history competes for it): the IR spectra as 16-bit pairs (one word
 * per bin and partition, multiplied with kmmawb.u / kmmawt.u: 32 x 16 bits, one instruction each) and the input
 * spectra as (re, im) pairs, both in rows of the partitions in use: 12 x P x 65 bytes (1024 taps: 12.5 KB). */
#ifndef CABQ_H
#define CABQ_H
#include <stdint.h>

#define CABQ_B        64
#define CABQ_N        128
#define CABQ_BINS     65
#ifndef CABQ_MAX_TAPS
#define CABQ_MAX_TAPS 1024
#endif
#define CABQ_MAX_P    (CABQ_MAX_TAPS / CABQ_B)
#define CABQ_IN       2

#if defined(__NDS32_EXT_DSP__)
#include <nds32_intrinsic.h>
#define QMUL(a, b)      __nds32__kwmmul_u((a), (b))
#define QMAC(c, a, b)   __nds32__kmmac_u((c), (a), (b))
#define QMSB(c, a, b)   __nds32__kmmsb_u((c), (a), (b))
#define HADD(a, b)      __nds32__raddw((a), (b))
#define HSUB(a, b)      __nds32__rsubw((a), (b))
#define QMB(c, a, w)    __nds32__kmmawb_u((c), (a), (w))     /* c + round(a x w.lo / 2^16) */
#define QMT(c, a, w)    __nds32__kmmawt_u((c), (a), (w))     /* c + round(a x w.hi / 2^16) */
#define QADD(a, b)      __nds32__kaddw((a), (b))             /* saturating */
#define QSUB(a, b)      __nds32__ksubw((a), (b))
#define CQ_PREF(p)      __nds32__dpref_w((unsigned int *)(p), 0, 0)   /* fetch this cache line ahead (a hint) */
#else
static inline int32_t cq_sat(int64_t v) { return v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (int32_t)0x80000000 : (int32_t)v; }
static inline int32_t QMUL(int32_t a, int32_t b) { return cq_sat(((int64_t)a * b + (1LL << 30)) >> 31); }
static inline int32_t QMAC(int32_t c, int32_t a, int32_t b) { return cq_sat((int64_t)c + (((int64_t)a * b + (1LL << 31)) >> 32)); }
static inline int32_t QMSB(int32_t c, int32_t a, int32_t b) { return cq_sat((int64_t)c - (((int64_t)a * b + (1LL << 31)) >> 32)); }
static inline int32_t HADD(int32_t a, int32_t b) { return (int32_t)(((int64_t)a + b) >> 1); }
static inline int32_t HSUB(int32_t a, int32_t b) { return (int32_t)(((int64_t)a - b) >> 1); }
static inline int32_t QMB(int32_t c, int32_t a, uint32_t w) { return cq_sat((int64_t)c + (((int64_t)a * (int16_t)w + 0x8000) >> 16)); }
static inline int32_t QMT(int32_t c, int32_t a, uint32_t w) { return cq_sat((int64_t)c + (((int64_t)a * (int16_t)(w >> 16) + 0x8000) >> 16)); }
static inline int32_t QADD(int32_t a, int32_t b) { return cq_sat((int64_t)a + b); }
static inline int32_t QSUB(int32_t a, int32_t b) { return cq_sat((int64_t)a - b); }
#define CQ_PREF(p)      ((void)(p))
#endif

typedef struct {
    int p, head;                             /* partitions in use (0: no IR, output = input); newest slot (ring runs downwards) */
    int out_shift;                           /* left shift that brings the output back to Q31 */
    int variant;                             /* bit 0: radix-4 FFTs (else radix 2), bit 1: products two partitions per pass (else one),
                                              * bit 2: prefetch the rows of the bin two ahead (the data comes from RAM: the model fills the cache) */
    int32_t prev[CABQ_B];                    /* the previous block's input (shifted) */
    uint32_t h[CABQ_BINS * CABQ_MAX_P];      /* IR partition spectra x 2^-eh, Q15 pairs (re low, im high), [bin][partition] */
    int32_t x[CABQ_BINS * CABQ_MAX_P * 2];   /* input spectra, (re, im) pairs, [bin][ring slot]; rows of p (in use) */
#ifdef CABQ_CYCLES
    uint32_t cyc_fwd, cyc_mac, cyc_inv;
#endif
} cabq_t;

static int32_t cq_c[64], cq_s[64];          /* cos / sin (2 pi k / 128), Q31 */
static int32_t cq_tw[2][21][3][2];          /* radix-4 twiddles W^j, W^2j, W^3j: [forward/inverse][stage offset + j][m-1][re,im] */
static uint8_t cq_rev4[64], cq_rev2[64];    /* the FFTs' output orders: base-4 digit reversal (radix 4), bit reversal (radix 2) */
static int32_t cq_tw2[2][63][2];            /* radix-2 per-stage twiddles */
static float cq_fc[128], cq_fs[128];         /* cos / sin (2 pi k / 128) as float, for cabq_set */

static int32_t cq_q31(double v) { double r = v * 2147483648.0; return r >= 2147483647.0 ? 2147483647 : r <= -2147483648.0 ? (int32_t)0x80000000 : (int32_t)(r < 0 ? r - 0.5 : r + 0.5); }

/* sin/cos in double for the tables (once): series on [0, pi/4], octant rotation */
static void cq_sincos(int k, double *c, double *s)    /* angle 2 pi k / 128 */
{
    int oct = k / 16, r = k % 16;
    double a = 3.14159265358979323846 * 2.0 * (double)r / 128.0, a2 = a * a;
    double sn = a * (1 - a2 / 6 * (1 - a2 / 20 * (1 - a2 / 42 * (1 - a2 / 72 * (1 - a2 / 110)))));
    double cs = 1 - a2 / 2 * (1 - a2 / 12 * (1 - a2 / 30 * (1 - a2 / 56 * (1 - a2 / 90 * (1 - a2 / 132)))));
    double S = 0.70710678118654752440, cc = 1, ss = 0;
    for (int i = 0; i < oct; i++) { double t = (cc - ss) * S; ss = (cc + ss) * S; cc = t; }
    *c = cs * cc - sn * ss; *s = sn * cc + cs * ss;
}

static void cabq_tables(void)
{
    for (int k = 0; k < 128; k++) { double c, s; cq_sincos(k, &c, &s); cq_fc[k] = (float)c; cq_fs[k] = (float)s; }
    for (int k = 0; k < 64; k++) {
        double c, s;
        cq_sincos(k, &c, &s);
        cq_c[k] = cq_q31(c); cq_s[k] = cq_q31(s);
        cq_rev4[k] = (uint8_t)((k & 3) << 4 | (k >> 2 & 3) << 2 | (k >> 4 & 3));
        int r = 0;
        for (int b = 0; b < 6; b++) if (k & (1 << b)) r |= 1 << (5 - b);
        cq_rev2[k] = (uint8_t)r;
    }
    for (int half = 1, i = 0; half <= 32; half <<= 1)
        for (int j = 0; j < half; j++, i++) {
            int t = j * (64 / half);
            cq_tw2[0][i][0] = cq_c[t]; cq_tw2[0][i][1] = (int32_t)-(int64_t)cq_s[t];
            cq_tw2[1][i][0] = cq_c[t]; cq_tw2[1][i][1] = cq_s[t];
        }
    /* stage L = 64: j = 0..15 at offset 0; L = 16: j = 0..3 at offset 16; L = 4 needs none (j = 0 only) */
    for (int L = 64, off = 0; L >= 16; off += L / 4, L >>= 2)
        for (int j = 0; j < L / 4; j++)
            for (int m = 1; m <= 3; m++) {
                double c, sn;
                cq_sincos((m * j * (128 / L)) & 127, &c, &sn);
                cq_tw[0][off + j][m - 1][0] = cq_q31(c); cq_tw[0][off + j][m - 1][1] = cq_q31(-sn);
                cq_tw[1][off + j][m - 1][0] = cq_q31(c); cq_tw[1][off + j][m - 1][1] = cq_q31(sn);
            }
}

/* complex x twiddle, Q31 */
#define CQ_ROT(r, i, w) do { int32_t r_ = (r), i_ = (i); \
    (r) = QMUL(r_, (w)[0]) - QMUL(i_, (w)[1]); (i) = QMUL(r_, (w)[1]) + QMUL(i_, (w)[0]); } while (0)

/* forward complex FFT of 64, radix 4, decimation in frequency: natural in, base-4 digit-reversed out; every stage
 * divides by 4 (two halvings), so the result is the FFT / 64 and nothing can overflow */
static void cq_fft64_dif4(int32_t *re, int32_t *im)
{
    for (int L = 64, off = 0; L >= 4; off += L / 4, L >>= 2) {
        int q = L / 4;
        for (int i0 = 0; i0 < 64; i0 += L)
            for (int j = 0; j < q; j++) {
                int a = i0 + j, b = a + q, c = b + q, d = c + q;
                int32_t t0r = HADD(re[a], re[c]), t0i = HADD(im[a], im[c]), t1r = HSUB(re[a], re[c]), t1i = HSUB(im[a], im[c]);
                int32_t t2r = HADD(re[b], re[d]), t2i = HADD(im[b], im[d]), t3r = HSUB(re[b], re[d]), t3i = HSUB(im[b], im[d]);
                re[a] = HADD(t0r, t2r); im[a] = HADD(t0i, t2i);                   /* y0 */
                int32_t y1r = HADD(t1r, t3i), y1i = HSUB(t1i, t3r);              /* t1 - i t3 */
                int32_t y2r = HSUB(t0r, t2r), y2i = HSUB(t0i, t2i);
                int32_t y3r = HSUB(t1r, t3i), y3i = HADD(t1i, t3r);              /* t1 + i t3 */
                if (j) {
                    int32_t (*w)[2] = cq_tw[0][off + j];
                    CQ_ROT(y1r, y1i, w[0]); CQ_ROT(y2r, y2i, w[1]); CQ_ROT(y3r, y3i, w[2]);
                }
                re[b] = y1r; im[b] = y1i; re[c] = y2r; im[c] = y2i; re[d] = y3r; im[d] = y3i;
            }
    }
}

/* inverse complex FFT of 64, radix 4, decimation in time: base-4 digit-reversed in, natural out, / 64 */
static void cq_fft64_dit_inv4(int32_t *re, int32_t *im)
{
    for (int L = 4, off = 20; L <= 64; L <<= 2, off = (L == 16) ? 16 : 0) {
        int q = L / 4;
        for (int i0 = 0; i0 < 64; i0 += L)
            for (int j = 0; j < q; j++) {
                int a = i0 + j, b = a + q, c = b + q, d = c + q;
                int32_t y0r = re[a], y0i = im[a], y1r = re[b], y1i = im[b], y2r = re[c], y2i = im[c], y3r = re[d], y3i = im[d];
                if (j) {
                    int32_t (*w)[2] = cq_tw[1][off + j];
                    CQ_ROT(y1r, y1i, w[0]); CQ_ROT(y2r, y2i, w[1]); CQ_ROT(y3r, y3i, w[2]);
                }
                int32_t p0r = HADD(y0r, y2r), p0i = HADD(y0i, y2i), p1r = HSUB(y0r, y2r), p1i = HSUB(y0i, y2i);
                int32_t p2r = HADD(y1r, y3r), p2i = HADD(y1i, y3i), p3r = HSUB(y1r, y3r), p3i = HSUB(y1i, y3i);
                re[a] = HADD(p0r, p2r); im[a] = HADD(p0i, p2i);
                re[c] = HSUB(p0r, p2r); im[c] = HSUB(p0i, p2i);
                re[b] = HSUB(p1r, p3i); im[b] = HADD(p1i, p3r);                  /* p1 + i p3 */
                re[d] = HADD(p1r, p3i); im[d] = HSUB(p1i, p3r);                  /* p1 - i p3 */
            }
    }
}

/* forward complex FFT of 64, decimation in frequency, natural in, bit-reversed out, halving every stage (/64) */
static void cq_fft64_dif2(int32_t *re, int32_t *im)
{
    for (int half = 32; half >= 4; half >>= 1) {
        int32_t (*w)[2] = cq_tw2[0] + (half - 1);
        for (int i = 0; i < 64; i += 2 * half)
            for (int j = 0; j < half; j++) {
                int a = i + j, b = a + half;
                int32_t dr = HSUB(re[a], re[b]), di = HSUB(im[a], im[b]);
                re[a] = HADD(re[a], re[b]); im[a] = HADD(im[a], im[b]);
                re[b] = QMUL(dr, w[j][0]) - QMUL(di, w[j][1]);
                im[b] = QMUL(dr, w[j][1]) + QMUL(di, w[j][0]);
            }
    }
    for (int i = 0; i < 64; i += 4) {                         /* half 2: twiddles 1 and -i */
        int32_t dr = HSUB(re[i], re[i + 2]), di = HSUB(im[i], im[i + 2]);
        re[i] = HADD(re[i], re[i + 2]); im[i] = HADD(im[i], im[i + 2]); re[i + 2] = dr; im[i + 2] = di;
        dr = HSUB(re[i + 1], re[i + 3]); di = HSUB(im[i + 1], im[i + 3]);
        re[i + 1] = HADD(re[i + 1], re[i + 3]); im[i + 1] = HADD(im[i + 1], im[i + 3]); re[i + 3] = di; im[i + 3] = -dr;
    }
    for (int i = 0; i < 64; i += 2) {                         /* half 1: twiddle 1 */
        int32_t dr = HSUB(re[i], re[i + 1]), di = HSUB(im[i], im[i + 1]);
        re[i] = HADD(re[i], re[i + 1]); im[i] = HADD(im[i], im[i + 1]); re[i + 1] = dr; im[i + 1] = di;
    }
}

/* inverse complex FFT of 64, decimation in time, bit-reversed in, natural out, halving every stage (/64) */
static void cq_fft64_dit_inv2(int32_t *re, int32_t *im)
{
    for (int i = 0; i < 64; i += 2) {
        int32_t tr = re[i + 1], ti = im[i + 1];
        re[i + 1] = HSUB(re[i], tr); im[i + 1] = HSUB(im[i], ti); re[i] = HADD(re[i], tr); im[i] = HADD(im[i], ti);
    }
    for (int i = 0; i < 64; i += 4) {
        int32_t tr = re[i + 2], ti = im[i + 2];
        re[i + 2] = HSUB(re[i], tr); im[i + 2] = HSUB(im[i], ti); re[i] = HADD(re[i], tr); im[i] = HADD(im[i], ti);
        tr = -im[i + 3]; ti = re[i + 3];                       /* x +i */
        re[i + 3] = HSUB(re[i + 1], tr); im[i + 3] = HSUB(im[i + 1], ti); re[i + 1] = HADD(re[i + 1], tr); im[i + 1] = HADD(im[i + 1], ti);
    }
    for (int half = 4; half <= 32; half <<= 1) {
        int32_t (*w)[2] = cq_tw2[1] + (half - 1);
        for (int i = 0; i < 64; i += 2 * half)
            for (int j = 0; j < half; j++) {
                int a = i + j, b = a + half;
                int32_t tr = QMUL(re[b], w[j][0]) - QMUL(im[b], w[j][1]), ti = QMUL(re[b], w[j][1]) + QMUL(im[b], w[j][0]);
                re[b] = HSUB(re[a], tr); im[b] = HSUB(im[a], ti);
                re[a] = HADD(re[a], tr); im[a] = HADD(im[a], ti);
            }
    }
}

/* real FFT of 128 -> 65 bins (natural order) with a stride; result = true FFT / 128 */
static void cq_rfft(const int32_t *x, int32_t *X, int stride, int r4)    /* bin k: X[k * stride] (re), X[k * stride + 1] (im) */
{
    const uint8_t *rev = r4 ? cq_rev4 : cq_rev2;
    int32_t zr[64], zi[64];
    for (int k = 0; k < 64; k++) { zr[k] = x[2 * k]; zi[k] = x[2 * k + 1]; }
    if (r4) cq_fft64_dif4(zr, zi); else cq_fft64_dif2(zr, zi);   /* Z / 64, Z[k] at rev[k] */
    X[0] = HADD(zr[0], zi[0]); X[1] = 0;
    X[64 * stride] = HSUB(zr[0], zi[0]); X[64 * stride + 1] = 0;
    for (int k = 1; k < 64; k++) {
        int p = rev[k], q = rev[64 - k];
        int32_t er = HADD(zr[p], zr[q]), ei = HSUB(zi[p], zi[q]);             /* (Z[k] + conj Z[64-k]) / 2 */
        int32_t or_ = HADD(zi[p], zi[q]), oi = HSUB(zr[q], zr[p]);            /* -i (Z[k] - conj Z[64-k]) / 2 */
        int32_t wr = cq_c[k], wi = (int32_t)-(int64_t)cq_s[k];
        X[k * stride] = HADD(er, QMUL(or_, wr) - QMUL(oi, wi));
        X[k * stride + 1] = HADD(ei, QMUL(or_, wi) + QMUL(oi, wr));
    }
}

/* inverse real FFT: 65 bins -> 128 samples; result = (unnormalised inverse) / 256 */
static void cq_irfft(const int32_t *Xr, const int32_t *Xi, int32_t *x, int r4)
{
    const uint8_t *rev = r4 ? cq_rev4 : cq_rev2;
    int32_t zr[64], zi[64];
    for (int k = 0; k < 64; k++) {
        int32_t ar = Xr[k], ai = Xi[k], br = Xr[64 - k], bi = -Xi[64 - k];
        int32_t er = HADD(ar, br), ei = HADD(ai, bi), dr = HSUB(ar, br), di = HSUB(ai, bi);
        int32_t wr = cq_c[k], wi = cq_s[k];
        int32_t or_ = QMUL(dr, wr) - QMUL(di, wi), oi = QMUL(dr, wi) + QMUL(di, wr);
        int r = rev[k];
        zr[r] = HSUB(er, oi); zi[r] = HADD(ei, or_);
    }
    if (r4) cq_fft64_dit_inv4(zr, zi); else cq_fft64_dit_inv2(zr, zi);
    for (int k = 0; k < 64; k++) { x[2 * k] = zr[k]; x[2 * k + 1] = zi[k]; }
}

/* load an IR (float taps; the main loop, once); n = 0: no IR */
static void cabq_set(cabq_t *c, const float *h, int n)
{
    static float spr[CABQ_MAX_P][CABQ_BINS], spi[CABQ_MAX_P][CABQ_BINS];
    if (n > CABQ_MAX_TAPS) n = CABQ_MAX_TAPS;
    c->p = (n + CABQ_B - 1) / CABQ_B;
    /* the partitions' spectra (a plain DFT in float with a table: about 70,000 multiply-adds per 1024 taps, a few ms on
     * the pedal), then one scale for all: the largest part just fits */
    double m = 1e-30;
    for (int p = 0; p < c->p; p++)
        for (int k = 0; k < CABQ_BINS; k++) {
            float re = 0, im = 0;
            for (int i = 0; i < CABQ_B && p * CABQ_B + i < n; i++) {
                int a = (k * i) & 127;
                re += h[p * CABQ_B + i] * cq_fc[a]; im -= h[p * CABQ_B + i] * cq_fs[a];
            }
            spr[p][k] = re; spi[p][k] = im;
            if (re > m) m = re; if (-re > m) m = -re; if (im > m) m = im; if (-im > m) m = -im;
        }
    int eh = 0;                                               /* m x 2^-eh in [0.5, 1) */
    while (m >= 1.0) { m *= 0.5; eh++; }
    while (m < 0.5 && eh > -(CABQ_IN + 9)) { m *= 2.0; eh--; }
    double sc = eh >= 0 ? 1.0 / (double)(1LL << eh) : (double)(1LL << -eh);
    for (int p = 0; p < c->p; p++)
        for (int k = 0; k < CABQ_BINS; k++) {
            double fr = spr[p][k] * sc * 32768.0, fi = spi[p][k] * sc * 32768.0;   /* Q15, rounded */
            int32_t r, i;
            r = fr >= 32767.0 ? 32767 : fr <= -32768.0 ? -32768 : (int32_t)(fr < 0 ? fr - 0.5 : fr + 0.5);
            i = fi >= 32767.0 ? 32767 : fi <= -32768.0 ? -32768 : (int32_t)(fi < 0 ? fi - 0.5 : fi + 0.5);
            c->h[k * c->p + p] = (uint32_t)(uint16_t)r | (uint32_t)(uint16_t)i << 16;
        }
    /* gain: input >> CABQ_IN; forward = true FFT / 128; products x 2^-eh / 2; inverse = unnormalised / 256 =
     * true inverse x 128 / 256. So out = y x 2^-(CABQ_IN + 9 + eh); eh >= -(CABQ_IN + 9) keeps the shift >= 0. */
    c->out_shift = CABQ_IN + 9 + eh;
    for (int k = 0; k < CABQ_BINS * CABQ_MAX_P * 2; k++) c->x[k] = 0;
    for (int i = 0; i < CABQ_B; i++) c->prev[i] = 0;
    c->head = 0;
}

/* one block: in[64] (Q31) -> out[64] (Q31; may be the same array) */
static void cabq_run(cabq_t *c, const int32_t *in, int32_t *out)
{
    int32_t buf[CABQ_N], Yr[CABQ_BINS], Yi[CABQ_BINS];
    int P = c->p, h;
    if (!P) { if (out != in) for (int i = 0; i < CABQ_B; i++) out[i] = in[i]; return; }
#ifdef CABQ_CYCLES
    uint32_t t0 = CABQ_CYCLES(), t1, t2;
#endif
    for (int i = 0; i < CABQ_B; i++) { int32_t v = in[i] >> CABQ_IN; buf[i] = c->prev[i]; buf[CABQ_B + i] = v; c->prev[i] = v; }
    h = c->head = c->head ? c->head - 1 : P - 1;
    cq_rfft(buf, &c->x[2 * h], 2 * P, c->variant & 1);
#ifdef CABQ_CYCLES
    t1 = CABQ_CYCLES();
#endif
    for (int k = 0; k < CABQ_BINS; k++) {                     /* Y[k] = sum_p X(p blocks old) H(p), halved */
        const int32_t *x = &c->x[2 * k * P];
        const uint32_t *hh = &c->h[k * P];
        int32_t rr = 0, ii = 0, ri = 0, ir = 0, rr2 = 0, ii2 = 0, ri2 = 0, ir2 = 0;
        int n1 = P - h, s, p;
        if ((c->variant & 4) && k + 2 < CABQ_BINS) {          /* the rows of bin k + 2, 32-byte lines */
            const char *px = (const char *)&c->x[2 * (k + 2) * P], *ph = (const char *)&c->h[(k + 2) * P];
            for (int o = 0; o < 8 * P; o += 32) CQ_PREF(px + o);
            for (int o = 0; o < 4 * P; o += 32) CQ_PREF(ph + o);
        }
        if (!(c->variant & 2)) {                              /* one partition per pass */
            for (s = h, p = 0; p < n1; s++, p++) {
                int32_t a = x[2 * s], b = x[2 * s + 1]; uint32_t w = hh[p];
                rr = QMB(rr, a, w); ii = QMT(ii, b, w); ri = QMT(ri, a, w); ir = QMB(ir, b, w);
            }
            for (s = 0; p < P; s++, p++) {
                int32_t a = x[2 * s], b = x[2 * s + 1]; uint32_t w = hh[p];
                rr = QMB(rr, a, w); ii = QMT(ii, b, w); ri = QMT(ri, a, w); ir = QMB(ir, b, w);
            }
        } else {
            /* re x re, im x im, re x im, im x re; two sets, two partitions per pass: the multiply-accumulates have a delay,
             * and with 8 sums each one waits for its own previous result only every other pass */
            for (s = h, p = 0; p + 1 < n1; s += 2, p += 2) {
                int32_t a = x[2 * s], b = x[2 * s + 1], a2 = x[2 * s + 2], b2 = x[2 * s + 3]; uint32_t w = hh[p], w2 = hh[p + 1];
                rr = QMB(rr, a, w); ii = QMT(ii, b, w); ri = QMT(ri, a, w); ir = QMB(ir, b, w);
                rr2 = QMB(rr2, a2, w2); ii2 = QMT(ii2, b2, w2); ri2 = QMT(ri2, a2, w2); ir2 = QMB(ir2, b2, w2);
            }
            if (p < n1) {
                int32_t a = x[2 * s], b = x[2 * s + 1]; uint32_t w = hh[p];
                rr = QMB(rr, a, w); ii = QMT(ii, b, w); ri = QMT(ri, a, w); ir = QMB(ir, b, w);
                p++;
            }
            for (s = 0; p + 1 < P; s += 2, p += 2) {
                int32_t a = x[2 * s], b = x[2 * s + 1], a2 = x[2 * s + 2], b2 = x[2 * s + 3]; uint32_t w = hh[p], w2 = hh[p + 1];
                rr = QMB(rr, a, w); ii = QMT(ii, b, w); ri = QMT(ri, a, w); ir = QMB(ir, b, w);
                rr2 = QMB(rr2, a2, w2); ii2 = QMT(ii2, b2, w2); ri2 = QMT(ri2, a2, w2); ir2 = QMB(ir2, b2, w2);
            }
            if (p < P) {
                int32_t a = x[2 * s], b = x[2 * s + 1]; uint32_t w = hh[p];
                rr = QMB(rr, a, w); ii = QMT(ii, b, w); ri = QMT(ri, a, w); ir = QMB(ir, b, w);
            }
        }
        Yr[k] = QSUB(QADD(rr, rr2), QADD(ii, ii2));
        Yi[k] = QADD(QADD(ri, ri2), QADD(ir, ir2));
    }
#ifdef CABQ_CYCLES
    t2 = CABQ_CYCLES();
#endif
    cq_irfft(Yr, Yi, buf, c->variant & 1);
    {
        int sh = c->out_shift;
        for (int i = 0; i < CABQ_B; i++) {
            int64_t v = (int64_t)buf[CABQ_B + i] << sh;
            out[i] = v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (int32_t)0x80000000 : (int32_t)v;
        }
    }
#ifdef CABQ_CYCLES
    c->cyc_fwd = t1 - t0; c->cyc_mac = t2 - t1; c->cyc_inv = CABQ_CYCLES() - t2;
#endif
}
#endif
