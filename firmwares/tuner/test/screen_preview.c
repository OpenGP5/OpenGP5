/* The tuner's screen on the computer (firmwares/tuner/test/screen.sh): the real nds32/firmware.c, its platform
 * calls replaced by a frame buffer, fed made-up tuner states. Writes one PPM per state. */
#include <stdio.h>
#include <string.h>
#include "gp5_firmware.h"
static uint16_t fb[GP5_SCREEN_W * GP5_SCREEN_H];
static void (*blk_fn)(const int32_t *, int32_t *);
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{ unsigned i, j; for (j = 0; j < h; j++) for (i = 0; i < w; i++) fb[(y + j) * GP5_SCREEN_W + x + i] = px[j * w + i]; }
void gp5_text(unsigned l, const char *s) { (void)l; (void)s; }
void gp5_link_start(void (*f)(const int32_t *, int32_t *)) { blk_fn = f; }
unsigned gp5_store_load(uint32_t id, void *d, unsigned m) { (void)id; (void)d; (void)m; return 0; }
int gp5_store_save(uint32_t id, const void *s, unsigned l) { (void)id; (void)s; (void)l; return 0; }
volatile uint32_t gp5_link_blocks;
#include "../nds32/firmware.c"
#include <math.h>
#include "../dsp/tuner.h"
static tuner_t tn;

/* play a plucked tone (f Hz, cents off) into the real tuner core for `sec`, hand its words to the screen side */
static double nsamp;
static void play(double f, double cents, double sec)
{
    int32_t blk[64], in[128], out[128];
    double fin = f * pow(2, cents / 1200);
    unsigned b, i, nb = (unsigned)(sec * 44100 / 64);
    for (b = 0; b < nb; b++) {
        for (i = 0; i < 64; i++, nsamp++) {
            double t = nsamp / 44100.0, v = 0; int h;
            if (f > 0) for (h = 1; h <= 8; h++) v += sin(2 * M_PI * fin * h * t + h) / (h * 1.3) * exp(-t * (0.3 + 0.2 * h));
            blk[i] = (int32_t)(v * 0.3 * 2147483647.0);
        }
        tuner_block(&tn, blk, 64);
    }
    memset(in, 0, sizeof in);
    in[LW_MAGIC] = (int32_t)LW_MAGIC_D; in[LW_NOTE] = tn.note; in[LW_CENTS] = (int32_t)(tn.cents * 10.0f);
    for (b = 0; b < TN_BANDS; b++) for (i = 0; i < TN_SEG; i += 4) {
        uint32_t w = 0, j;
        for (j = 0; j < 4; j++) w |= (uint32_t)(tn.seg[b][i + j] * 255.0f + 0.5f) << (8 * j);
        in[LW_SEG + (b * TN_SEG + i) / 4] = (int32_t)w;
    }
    blk_fn(in, out);
}
static void restart(void) { tuner_init(&tn, 440.0f); nsamp = 0; }
static void shot(const char *dir, const char *name)
{
    char p[512]; FILE *f; unsigned i;
    while (fw_draw(0, 0)) ;
    fw_knob(0); fw_draw(0, 0);
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name); f = fopen(p, "wb");
    fprintf(f, "P6 160 80 255\n");
    for (i = 0; i < 160 * 80; i++) {                      /* panel pixel (BGR 5-6-5, bytes swapped) -> RGB */
        unsigned v = (uint16_t)((fb[i] >> 8) | (fb[i] << 8)), b = v >> 11, g = (v >> 5) & 63, r = v & 31;
        fputc(r << 3 | r >> 2, f); fputc(g << 2 | g >> 4, f); fputc(b << 3 | b >> 2, f);
    }
    fclose(f);
}
int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    fw_init(); fw_draw(0, 1);
    restart(); play(0, 0, 0.3); shot(d, "1_no_note");
    restart(); play(82.41, 0, 1.0); shot(d, "2_E2_in_tune");
    restart(); play(110.0, 12, 1.0); shot(d, "3_A2_sharp_12");
    restart(); play(196.0, -29, 1.0); shot(d, "4_G3_flat_29");
    restart(); play(277.18, -4, 1.0); shot(d, "5_C#4_flat_4");
    fw_foot(); restart(); play(82.41, 0, 1.0); shot(d, "6_muted"); fw_foot();
    fw_para_turn(2); shot(d, "7_reference_442");
    return 0;
}
