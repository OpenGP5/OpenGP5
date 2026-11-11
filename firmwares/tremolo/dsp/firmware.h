/* Tremolo: the volume rises and falls with a sine wave. The worked example of docs/platform/first-effect.md:
 * float arithmetic, sinf from gp5_math.h, ordinary variables, and settings from the control half.
 *
 * Settings (link command 0x7F10 [id u32][value u32], sent by the control half's fw_take):
 *   0  rate    in 1/100 Hz, 50 .. 1500 (0.5 .. 15 Hz)
 *   1  depth   0 .. 255 (none .. the volume dips to silence)
 *   2  on      0 = the guitar goes straight through, 1 = tremolo */
#include "gp5_math.h"

static float phase;              /* 0 .. 1, one full wave */
static float step;               /* phase per sample = rate / sample rate */
static float depth, depth_now;   /* target and smoothed depth, 0 .. 1 */
static int on;

static void set_rate(uint32_t centi_hz)
{
    if (centi_hz < 50) centi_hz = 50;
    if (centi_hz > 1500) centi_hz = 1500;
    step = (float)centi_hz / 100.0f / (float)GP5_SAMPLE_RATE;
}

static void fw_init(void)
{
    phase = 0.0f;
    set_rate(500);               /* 5 Hz */
    depth = depth_now = 0.5f;
    on = 1;
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    float target = on ? depth : 0.0f;
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        depth_now += 0.001f * (target - depth_now);              /* glide, so changes do not click */
        float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);   /* 0 .. 1 */
        float gain = 1.0f - depth_now * wave;
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        int32_t y = (int32_t)((float)in[i] * gain);
        out_left[i] = y;
        out_right[i] = y;
    }
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)
{
    if (cmd != 0x7F10 || n < 12) return 0;
    uint32_t id = gp5_get32(p + 4), value = gp5_get32(p + 8);
    if (id == 0) set_rate(value);
    else if (id == 1) depth = (float)(value > 255 ? 255 : value) / 255.0f;
    else if (id == 2) on = value != 0;
    gp5_ack();
    return 1;
}
