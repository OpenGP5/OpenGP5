/* Strobe tuner, a demo firmware built with OpenGP5. DSP side: the guitar straight through, plus a strobe tuner (tuner.h).
 * The tuner's results go to the control processor over the audio link (gp5_dsp.h, GP5_AUDIO_LINK), which draws
 * them; it sends back the reference pitch and whether the output is muted (foot switch). */
#define GP5_AUDIO_LINK
#include "gp5_math.h"
#include "tuner.h"
#include "link_words.h"

static tuner_t tn;
static int mute;
static uint32_t tn_cycles, tn_worst;                   /* the tuner's own share of a block (gp5_link.py raw 307f0400) */                                   /* kept when a block from the control processor is missing */

static void fw_init(void) { tuner_init(&tn, 440.0f); mute = 0; }

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    const int32_t *li = gp5_link_in();
    int32_t *lo = gp5_link_out();
    uint32_t i;
    if ((uint32_t)li[LW_MAGIC] == LW_MAGIC_N) {             /* settings from the control processor */
        float a4 = (float)li[LW_A4] * 0.1f;
        if (a4 >= 400.0f && a4 <= 480.0f && a4 != tn.a4) { tn.a4 = a4; if (tn.note >= 0) tuner_set_ref(&tn, tn.note); }
        mute = li[LW_MUTE] != 0;
    }
    for (i = 0; i < GP5_BLOCK; i++) { int32_t v = mute ? 0 : in[i]; out_left[i] = v; out_right[i] = v; }
#ifndef GP5_HOST
    {
        uint32_t t0 = *(volatile uint32_t *)0xE0001004u;      /* cycle counter */
        tuner_block(&tn, in, GP5_BLOCK);
        tn_cycles = *(volatile uint32_t *)0xE0001004u - t0;
        if (tn_cycles > tn_worst) tn_worst = tn_cycles;
    }
#else
    tuner_block(&tn, in, GP5_BLOCK);
#endif
    lo[LW_MAGIC] = (int32_t)LW_MAGIC_D;
    lo[LW_NOTE] = tn.note;
    lo[LW_CENTS] = (int32_t)(tn.cents * 10.0f);
    for (i = 0; i < TN_ROWS; i++) {
        lo[LW_PHASE + i] = (int32_t)(tn.phase[i] * 65536.0f);
        lo[LW_AMP + i] = (int32_t)(tn.amp[i] * 65536.0f);
    }
    {
        uint32_t b, g;
        for (b = 0; b < TN_BANDS; b++)
            for (g = 0; g < TN_SEG; g += 4) {
                uint32_t w = 0, j;
                for (j = 0; j < 4; j++) w |= (uint32_t)(tn.seg[b][g + j] * 255.0f + 0.5f) << (8 * j);
                lo[LW_SEG + (b * TN_SEG + g) / 4] = (int32_t)w;
            }
    }
    lo[LW_LEVEL] = (int32_t)(tn.level_db * 10.0f);
    lo[LW_FREQ] = (int32_t)(tn.freq * 100.0f);
}

static int fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n)
{
    (void)payload; (void)n;
    if (cmd == 0x7F30) {                                    /* [cmd][len 12][tuner cycles last block][worst] */
        uint8_t r[12] = { 0x30, 0x7F, 12, 0 };
        uint32_t i;
        for (i = 0; i < 4; i++) { r[4 + i] = (uint8_t)(tn_cycles >> (8 * i)); r[8 + i] = (uint8_t)(tn_worst >> (8 * i)); }
        gp5_reply(r, sizeof r);
        return 1;
    }
    return 0;
}
