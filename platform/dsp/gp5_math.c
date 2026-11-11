/* Single-precision maths for DSP firmwares: declared in gp5_math.h. Plain C, no tables, no C library.
 * Also compiled on the computer by tools/test_gp5_math.py, which compares every function with the C library. */
#include <stdint.h>
#include "gp5_math.h"

static inline float bits_to_f(uint32_t u) { union { uint32_t u; float f; } v; v.u = u; return v.f; }
static inline uint32_t f_to_bits(float f) { union { uint32_t u; float f; } v; v.f = f; return v.u; }

float floorf(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f)) return x;          /* already whole (or NaN / inf) */
    float t = (float)(int32_t)x;
    return t > x ? t - 1.0f : t;
}

float fmodf(float x, float y)
{
    if (y == 0.0f) return 0.0f;
    float q = x / y;
    q = q < 0.0f ? -floorf(-q) : floorf(q);                        /* truncate towards zero */
    return x - q * y;
}

/* sin on [-pi/2, pi/2]: odd polynomial (Taylor to x^11; error below 1e-7 there) */
static float sin_core(float x)
{
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.6666667e-1f + x2 * (8.3333333e-3f + x2 * (-1.9841270e-4f
                     + x2 * (2.7557319e-6f + x2 * -2.5052108e-8f)))));
}

/* x reduced to [-pi, pi], with 2*pi split in two so that k * 2*pi is exact for the first part */
static float reduce(float x)
{
    float k = floorf(x * 0.159154943f + 0.5f);
    return (x - k * 6.28125f) - k * 1.93530717958e-3f;
}

float sinf(float x)
{
    x = reduce(x);
    if (x > 1.57079633f) x = 3.14159265f - x;
    else if (x < -1.57079633f) x = -3.14159265f - x;
    return sin_core(x);
}

float cosf(float x) { return sin_core(1.57079633f - fabsf(reduce(x))); }

float tanf(float x) { return sinf(x) / cosf(x); }

float exp2f(float x)
{
    if (x > 127.9f) return bits_to_f(0x7F800000u);                 /* inf */
    if (x < -126.0f) return 0.0f;
    float n = floorf(x + 0.5f), r = x - n;                         /* r in [-0.5, 0.5] */
    float t = r * 0.693147181f;                                    /* e^t = 2^r, |t| <= 0.35 */
    float p = 1.0f + t * (1.0f + t * (0.5f + t * (1.6666667e-1f + t * (4.1666667e-2f
                     + t * (8.3333333e-3f + t * 1.3888889e-3f)))));
    return p * bits_to_f((uint32_t)((int32_t)n + 127) << 23);
}

float expf(float x)
{
    if (x > 88.72f) return bits_to_f(0x7F800000u);
    if (x < -87.33f) return 0.0f;
    float n = floorf(x * 1.44269504f + 0.5f);
    float t = (x - n * 0.693359375f) + n * 2.12194440e-4f;          /* x - n*ln2, ln2 split in two */
    float p = 1.0f + t * (1.0f + t * (0.5f + t * (1.6666667e-1f + t * (4.1666667e-2f
                     + t * (8.3333333e-3f + t * 1.3888889e-3f)))));
    return p * bits_to_f((uint32_t)((int32_t)n + 127) << 23);
}

float log2f(float x)
{
    if (!(x > 0.0f)) return x == 0.0f ? -bits_to_f(0x7F800000u) : bits_to_f(0x7FC00000u);
    uint32_t u = f_to_bits(x);
    int32_t e = (int32_t)((u >> 23) & 0xFF);
    if (e == 0) { x *= 8388608.0f; u = f_to_bits(x); e = (int32_t)((u >> 23) & 0xFF) - 23; }   /* denormal */
    if (e == 0xFF) return x;                                       /* inf */
    float m = bits_to_f((u & 0x007FFFFFu) | 0x3F800000u);         /* m in [1, 2) */
    e -= 127;
    if (m > 1.41421356f) { m *= 0.5f; e++; }                       /* m in [0.707, 1.414) */
    float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;                 /* |s| < 0.172 */
    float ln = 2.0f * s * (1.0f + s2 * (3.3333333e-1f + s2 * (2.0e-1f + s2 * (1.4285714e-1f + s2 * 1.1111111e-1f))));
    return (float)e + ln * 1.44269504f;
}

float logf(float x) { return log2f(x) * 0.693147181f; }
float log10f(float x) { return log2f(x) * 0.301029996f; }

float powf(float x, float y)
{
    if (y == 0.0f) return 1.0f;
    if (x == 0.0f) return y > 0.0f ? 0.0f : bits_to_f(0x7F800000u);
    if (x > 0.0f) return exp2f(y * log2f(x));
    float yi = floorf(y);
    if (yi != y) return bits_to_f(0x7FC00000u);                    /* negative base, fractional power */
    float r = exp2f(y * log2f(-x));
    return ((int32_t)yi & 1) ? -r : r;
}

float tanhf(float x)
{
    float a = fabsf(x);
    if (a > 9.0f) return x > 0.0f ? 1.0f : -1.0f;
    if (a < 0.3f) {                                                /* series: no cancellation near 0 */
        float x2 = x * x;
        return x * (1.0f + x2 * (-3.3333333e-1f + x2 * (1.3333333e-1f + x2 * (-5.3968254e-2f + x2 * 2.1869489e-2f))));
    }
    float e = expf(2.0f * a);
    float t = (e - 1.0f) / (e + 1.0f);
    return x < 0.0f ? -t : t;
}
