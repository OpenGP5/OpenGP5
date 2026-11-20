/* The tremolo's screen on the computer: the control half with the platform's parameters, text and a fake store,
 * drawn into a frame buffer and saved as PPM pictures (screen.sh turns them into PNGs).  firmwares/tremolo/test/screen.sh */
#include <stdio.h>
#include <string.h>
#include "gp5_firmware.h"
static uint16_t fb[GP5_SCREEN_W * GP5_SCREEN_H];
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px) { for (unsigned j = 0; j < h; j++) memcpy(&fb[(y + j) * GP5_SCREEN_W + x], px + j * w, w * 2); }
void gp5_text(unsigned l, const char *s) { (void)l; (void)s; }
void gp5_led(uint32_t c) { (void)c; }
static uint32_t ms; uint32_t gp5_ms(void) { return ms; }
static unsigned knob; unsigned gp5_knob(void) { return knob; }
const char *fw_name(void) { return "tremolo"; }
static struct { uint32_t id; unsigned len; uint8_t d[1024]; } recs[32]; static unsigned nrec;
unsigned gp5_store_load(uint32_t id, void *d, unsigned m) { for (unsigned i = 0; i < nrec; i++) if (recs[i].id == id) { memcpy(d, recs[i].d, recs[i].len < m ? recs[i].len : m); return recs[i].len; } return 0; }
int gp5_store_save(uint32_t id, const void *s, unsigned l) { unsigned i; for (i = 0; i < nrec && recs[i].id != id; i++) {} if (i == nrec) nrec++; recs[i].id = id; recs[i].len = l; memcpy(recs[i].d, s, l); return 0; }
#include "../nds32/firmware.c"
#include "gp5_params.c"
#include "gp5_ui.c"
void gp5_params_init(void);
static void shot(const char *dir, const char *name)
{
    char p[512]; FILE *f; while (fw_draw(0)) ;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name); f = fopen(p, "wb"); fprintf(f, "P6 %d %d 255\n", GP5_SCREEN_W, GP5_SCREEN_H);
    for (int i = 0; i < GP5_SCREEN_W * GP5_SCREEN_H; i++) {   /* the panel's pixels: blue in the top bits */
        unsigned v = (uint16_t)((fb[i] >> 8) | (fb[i] << 8)), b = v >> 11, g = (v >> 5) & 63, r = v & 31;
        fputc(r << 3 | r >> 2, f); fputc(g << 2 | g >> 4, f); fputc(b << 3 | b >> 2, f);
    }
    fclose(f);
}
int main(int argc, char **argv)
{
    gp5_params_init(); fw_init(); fw_draw(1);
    shot(argv[1], "1_start");
    fw_para_click(); fw_para_turn(5); shot(argv[1], "2_adjusting_rate");
    fw_para_click(); fw_para_turn(1); shot(argv[1], "3_depth_picked");
    fw_para_turn(2); fw_para_click(); fw_para_turn(1); fw_para_hold(); shot(argv[1], "4_saved");
    ms += 2000; shot(argv[1], "5_preset_row");
    fw_para_click(); fw_para_turn(-2); fw_para_click(); fw_para_turn(3); fw_para_click(); fw_para_turn(2); shot(argv[1], "6_changed_since_preset");
    fw_foot_down(); fw_para_click(); shot(argv[1], "7_off");
    (void)argc; return 0;
}
