/* Tremolo, DSP half: the volume rises and falls with a wave. The example firmware of OpenGP5 (with
 * ../nds32/firmware.c and ../params.h): float arithmetic, sinf from gp5_math.h, ordinary variables, parameters.
 * Its settings arrive as parameters (../params.h): fw_param() hears each change, gp5_param() reads the value. */
#include "gp5_math.h"

static float phase;              /* 0 .. 1, one full wave */
static float step;               /* phase per sample = rate / sample rate */
static float depth_now;          /* the depth actually used, gliding to the setting so changes don't click */

static void set_rate(int32_t centi_hz) { step = (float)centi_hz / 100.0f / (float)GP5_SAMPLE_RATE; }

static void fw_init(void)
{
    phase = 0.0f;
    set_rate(gp5_param(GP5_P_RATE));             /* the defaults from params.h, until the control half sends its own */
    depth_now = 0.0f;
}

static void fw_param(uint32_t id, int32_t value)
{
    if (id == GP5_P_RATE) set_rate(value);       /* DEPTH, SHAPE and ON are read where they are used */
}

static float wave(float ph, int shape)           /* 0 .. 1 */
{
    if (shape == 1) return ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;       /* triangle */
    if (shape == 2) return ph < 0.5f ? 1.0f : 0.0f;                         /* square */
    return 0.5f + 0.5f * sinf(2.0f * GP5_PI * ph);                          /* sine */
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    float target = gp5_param(GP5_P_ON) ? (float)gp5_param(GP5_P_DEPTH) / 100.0f : 0.0f;
    int shape = gp5_param(GP5_P_SHAPE);
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        depth_now += 0.001f * (target - depth_now);
        float gain = 1.0f - depth_now * wave(phase, shape);
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        int32_t y = (int32_t)((float)in[i] * gain);
        out_left[i] = y;
        out_right[i] = y;
    }
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n) { (void)cmd; (void)p; (void)n; return 0; }   /* none of its own */
