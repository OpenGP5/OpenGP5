/* NAM A2 Lite with a 32-bit residual stream, for the control processor (NDS32, DSP extension).
 *
 * Why: the 16-bit kernel (an earlier design) keeps each layer's input (the residual stream) in int16.
 * In a high-gain capture that stream swings to ~+-600 when playing, so 16 bits leave a step of 1/16: quiet
 * input (idle hiss) comes out grainy (14 dB from float at -70 dBFS). Here the stream is int32 (step ~2^-20),
 * the post-activation values stay int32, and every multiply is 32 x 16 bits with rounding (KMMAWB.u: one per
 * instruction, but no zero padding lane as in the 16-bit pairs).
 *
 * Formats (value = q * 2^-e), chosen by a2r_export.py:
 *   x  residual / layer input   int32, e = X (one for all layers)
 *   u  guitar input              int32, e = 31 (full precision from the DSP; mix and rech are 32 x 32)
 *   W  conv weights              int16, e = ew = ez - X + 16      z = b + mix u + sum W x  (int32, e = ez)
 *   a  LeakyReLU(z)              int32, e = ez
 *   r  1x1: l1b + l1w a          int32, e = er = ez + e1 - 16;  x += r shifted from er to X
 *   head sum                     int32, e = eh;  head conv int16 weights (e = ehw), int64 accumulator */
#ifndef A2R_H
#define A2R_H
#include <stdint.h>

#define A2R_C 3
#define A2R_BLOCK 64
#ifndef A2R_MAX_LAYERS
#define A2R_MAX_LAYERS 24
#endif
#ifndef A2R_MAX_KH
#define A2R_MAX_KH 16
#endif

typedef struct {
    uint8_t K; uint16_t D;
    int8_t ez, er;                      /* conv / activation exponent, 1x1 output exponent */
    const int16_t *w;                   /* conv [o][i][k], e = ez - X + 16 */
    int32_t b[A2R_C], mix[A2R_C];       /* b: e = ez; mix * 2^(ez+1): (mix * u) >> 32 */
    int16_t l1w[A2R_C * A2R_C];         /* [o][i], e = er - ez + 16 */
    int32_t l1b[A2R_C];                 /* e = er */
} a2r_layer_def_t;

typedef struct {
    int nl, kh;
    int32_t slope_q16;
    int8_t X;
    int32_t rech[A2R_C];                /* rech * 2^(X+1): x0 = (rech * u) >> 32 */
    int8_t eh, ehw;
    const int16_t *hw;                  /* head conv [c][k] */
    float hb, hs;
    const a2r_layer_def_t *layers;
} a2r_model_t;

typedef struct {
    int32_t *ring;                      /* linear history, rows of 3 int32 */
    uint32_t wpk[5 * 15];               /* per tap: 9 int16 W[o][i] in 5 words (low half first) */
    int len, pos, K, D;
    int8_t sh_head, sh_r;               /* sh_r > 0: right shift, < 0: left shift (er -> X) */
    int32_t b[A2R_C], mix[A2R_C], l1b[A2R_C];
    uint32_t l1p[5];                    /* 9 int16 l1w[o][i] */
} a2r_layer_rt_t;

typedef struct {
    const a2r_model_t *m;
    int l0, l1;                         /* the layers this chip runs */
    a2r_layer_rt_t L[A2R_MAX_LAYERS];
    int32_t hbuf[(A2R_MAX_KH - 1 + A2R_BLOCK) * A2R_C];
    float out_k, out_b;                 /* Q31 out = y * out_k + out_b */
} a2r_state_t;

int a2r_mem_bytes(const a2r_model_t *m);
int a2r_mem_bytes_range(const a2r_model_t *m, int l0, int l1);
int a2r_bind(a2r_state_t *s, const a2r_model_t *m, void *mem, int bytes);
int a2r_bind_range(a2r_state_t *s, const a2r_model_t *m, int l0, int l1, void *mem, int bytes);
void a2r_run(a2r_state_t *s, const int32_t *u, const int32_t *xin, int32_t *xout, int32_t *out, int bias);
void a2r_reset(a2r_state_t *s);
void a2r_prime(a2r_state_t *s);
void a2r_process(a2r_state_t *s, const int32_t *in, int32_t *out);    /* one block: Q31 in, Q31 out */
#endif
