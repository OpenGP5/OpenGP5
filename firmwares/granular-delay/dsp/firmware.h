/* Granular delay / micro-looper: the DSP half of the firmware (platform interface: gp5_dsp.h).
 * The effect state is an ordinary variable; both recordings are in the large RAM block (GP5_LARGE):
 * delay 22528 samples (0.51 s at 44.1 kHz), loop 39936 samples (0.91 s). Link commands:
 *   0x7F10 set:  [10 7f][0c 00][param id u32][value u32] -> status ACK
 *   0x7F11 get:  [11 7f][04 00] -> [11 7f][len][13 params][delay write index][loop write index]
 *                [grains started][grains dropped][cycles last block][cycles worst block][late blocks] */
#include "granular.h"
#define GR_DLEN 22528u
#define GR_LLEN 39936u
static gr_t gr;
static struct { int16_t delay[GR_DLEN]; int16_t loop[GR_LLEN]; } rec GP5_LARGE;   /* one block: fixed order */
#define GR (&gr)
#define GR_DBUF rec.delay
#define GR_LBUF rec.loop

static void fw_init(void) { gr_init(GR, GR_DBUF, GR_DLEN, GR_LBUF, GR_LLEN); }

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    gr_block(GR, in, out_left, out_right, GP5_BLOCK);
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)
{
    if (cmd == 0x7F10 && n >= 12) { gr_set(GR, gp5_get32(p + 4), gp5_get32(p + 8)); gp5_ack(); return 1; }
    if (cmd == 0x7F11) {
        uint8_t r[4 + 4 * (GR_NP + 7)] = {0x11, 0x7F, (uint8_t)sizeof r, 0};
        uint32_t v[GR_NP + 7];
        for (uint32_t i = 0; i < GR_NP; i++) v[i] = GR->p[i];
        v[GR_NP] = GR->d.w; v[GR_NP + 1] = GR->l.w; v[GR_NP + 2] = GR->spawned; v[GR_NP + 3] = GR->dropped;
        v[GR_NP + 4] = GP5_CYCLES_LAST; v[GR_NP + 5] = GP5_CYCLES_WORST; v[GR_NP + 6] = GP5_LATE_BLOCKS;
        for (uint32_t i = 0; i < GR_NP + 7; i++)
            for (uint32_t k = 0; k < 4; k++) r[4 + 4 * i + k] = (uint8_t)(v[i] >> (8 * k));
        gp5_reply(r, sizeof r);
        return 1;
    }
    return 0;
}
