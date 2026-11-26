/* 24-bit block floating point for the audio link (both sides): a run of int32 values goes over as 24-bit numbers,
 * 4 values in 3 words, each value shifted right by its group's shift (chosen per block from the group's largest
 * value, so that it fits). The error is at most one step of 2^shift: about 2^-23 of the block's largest value
 * (-138 dB), and nothing at all for values that fit 24 bits already (shift 0). Quiet blocks keep every bit.
 * Groups: a run is one group, or 3 interleaved groups (rows of 3: the residual stream's channels). */
#ifndef NAM_PACK24_H
#define NAM_PACK24_H
#include <stdint.h>

/* the shift that makes every value of v[g], v[g + step], ... fit 24 bits signed */
__attribute__((unused)) static unsigned p24_shift(const int32_t *v, int n, int g, int step)
{
    uint32_t m = 0, a;
    unsigned s = 0;
    for (int i = g; i < n; i += step) { a = v[i] < 0 ? ~(uint32_t)v[i] : (uint32_t)v[i]; m |= a; }
    while ((m >> s) > 0x7FFFFFu) s++;
    return s;
}

/* n values (a multiple of 4) -> 3n/4 words; value i is shifted by sh[i % groups] */
__attribute__((unused)) static void p24_pack(const int32_t *v, int n, const unsigned *sh, int groups, int32_t *o)
{
    for (int i = 0; i < n; i += 4, o += 3) {
        uint32_t a = (uint32_t)(v[i] >> sh[i % groups]) & 0xFFFFFFu, b = (uint32_t)(v[i + 1] >> sh[(i + 1) % groups]) & 0xFFFFFFu;
        uint32_t c = (uint32_t)(v[i + 2] >> sh[(i + 2) % groups]) & 0xFFFFFFu, d = (uint32_t)(v[i + 3] >> sh[(i + 3) % groups]) & 0xFFFFFFu;
        o[0] = (int32_t)(a | b << 24);
        o[1] = (int32_t)(b >> 8 | c << 16);
        o[2] = (int32_t)(c >> 16 | d << 8);
    }
}

static int32_t p24_get(uint32_t x, unsigned s) { return (int32_t)(((int32_t)(x << 8) >> 8) * (int32_t)(1u << s)); }

__attribute__((unused)) static void p24_unpack(const int32_t *w, int n, const unsigned *sh, int groups, int32_t *v)
{
    for (int i = 0; i < n; i += 4, w += 3) {
        uint32_t w0 = (uint32_t)w[0], w1 = (uint32_t)w[1], w2 = (uint32_t)w[2];
        v[i]     = p24_get(w0, sh[i % groups]);
        v[i + 1] = p24_get(w0 >> 24 | w1 << 8, sh[(i + 1) % groups]);
        v[i + 2] = p24_get(w1 >> 16 | w2 << 16, sh[(i + 2) % groups]);
        v[i + 3] = p24_get(w2 >> 8, sh[(i + 3) % groups]);
    }
}
#endif
