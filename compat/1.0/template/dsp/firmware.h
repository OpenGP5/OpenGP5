/* The sound: runs on the DSP, 64 samples at a time (opengp5/platform/dsp/gp5_dsp.h, opengp5/docs/platform/api.md).
 * An overdrive: gain, a soft clip (tanhf), a one-pole low-pass, the output level. Settings come from ../params.h:
 * fw_param() hears each change, gp5_param() reads the current value. */
#include "gp5_math.h"

static float gain, gain_now;       /* drive as a factor, and the one in use (glides, so turning never clicks) */
static float level, level_now;
static float tone_a, lp;           /* low-pass coefficient and its state */
static float mix_now;              /* 0 = bypassed, 1 = on, gliding between */

static float db10(int32_t v) { return powf(10.0f, (float)v / 200.0f); }   /* tenths of a dB -> a factor */

static void fw_param(uint32_t id, int32_t v)
{
    if (id == GP5_P_DRIVE) gain = db10(v);
    if (id == GP5_P_LEVEL) level = db10(v);
    if (id == GP5_P_TONE) tone_a = 1.0f - expf(-2.0f * GP5_PI * (float)v / (float)GP5_SAMPLE_RATE);
}

static void fw_init(void)
{
    fw_param(GP5_P_DRIVE, gp5_param(GP5_P_DRIVE));        /* params.h's defaults, until the control half sends its own */
    fw_param(GP5_P_LEVEL, gp5_param(GP5_P_LEVEL));
    fw_param(GP5_P_TONE, gp5_param(GP5_P_TONE));
    gain_now = gain; level_now = level; lp = 0.0f; mix_now = 1.0f;
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    float on = gp5_param(GP5_P_ON) ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        float x = (float)in[i] / GP5_FULL_SCALE;
        gain_now += 0.002f * (gain - gain_now);
        level_now += 0.002f * (level - level_now);
        mix_now += 0.002f * (on - mix_now);
        lp += tone_a * (tanhf(x * gain_now) - lp);
        float y = x + mix_now * (lp * level_now - x);        /* a smooth switch between dry and the effect */
        if (y > 0.999f) y = 0.999f;
        if (y < -0.999f) y = -0.999f;
        out_left[i] = out_right[i] = (int32_t)(y * GP5_FULL_SCALE);
    }
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n) { (void)cmd; (void)p; (void)n; return 0; }   /* none */
