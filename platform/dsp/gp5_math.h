/* Single-precision maths for DSP firmwares (gp5_math.c). The standard C names, so effect code reads as usual;
 * the DSP has no C library, these are the platform's own. All take and return float (use 1.0f, not 1.0:
 * double arithmetic is done in software and is slow). Accuracy: about 1e-6 relative for sinf / cosf /
 * expf / logf over normal audio ranges; tests: tools/test_gp5_math.py. sqrtf and fabsf are single
 * instructions. */
#ifndef GP5_MATH_H
#define GP5_MATH_H

#define GP5_PI 3.14159265358979f

float sinf(float x);
float cosf(float x);
float tanf(float x);
float expf(float x);
float exp2f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float powf(float x, float y);
float tanhf(float x);
float floorf(float x);
float fmodf(float x, float y);
#define sqrtf(x) __builtin_sqrtf(x)
#define fabsf(x) __builtin_fabsf(x)
#define fminf(a, b) __builtin_fminf(a, b)
#define fmaxf(a, b) __builtin_fmaxf(a, b)

/* decibels <-> gain */
static inline float gp5_db_to_gain(float db) { return expf(db * 0.115129255f); }      /* 10^(db/20) */
static inline float gp5_gain_to_db(float g) { return 8.68588964f * logf(g); }          /* 20*log10(g) */

#endif
