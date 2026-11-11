/* Host mock-up of the effect screen: renders states through ui.c + fx_ui.c into raw RGB files.
 * Used by tools/ui_mock.py, which turns them into one PNG sheet. */
#include <stdio.h>
#include <string.h>
#include "../ui.h"
#include "../fx_ui.h"

static uint16_t fb[UI_W * UI_H];
void ui_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{
    for (unsigned j = 0; j < h; j++) for (unsigned i = 0; i < w; i++) fb[(y + j) * UI_W + x + i] = px[j * w + i];
}

static void shot(const char *dir, const char *name, unsigned knob)
{
    ui_state_t s; char path[512]; FILE *f;
    s.name = fx_ui_name(); fx_ui_value(s.value); s.frac = (uint8_t)fx_ui_frac(); s.knob = (uint8_t)knob;
    s.live = (uint8_t)fx_ui_live(); s.on = (uint8_t)fx_ui_on(); s.frozen = (uint8_t)fx_ui_frozen();
    s.sel = (uint8_t)fx_ui_sel(); s.count = FX_UI_COUNT;
    for (int f = 1; ui_draw(&s, f); f = 0) {}
    snprintf(path, sizeof path, "%s/%s.rgb", dir, name);
    f = fopen(path, "wb");
    for (unsigned i = 0; i < UI_W * UI_H; i++) {
        unsigned v = fb[i], r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
        fputc((int)(r | r >> 5), f); fputc((int)(g | g >> 6), f); fputc((int)(b | b >> 5), f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    fx_ui_init();
    fx_ui_knob(200); fx_ui_knob(200);
    shot(d, "1_off", 200);                                   /* power-on: off, MIX chosen, knob waiting */
    fx_ui_para_push();
    shot(d, "2_on_mix_waiting", 200);
    fx_ui_knob(170);
    shot(d, "3_mix_following_knob", 170);
    fx_ui_turn(1);
    shot(d, "4_delay_chosen", 170);
    fx_ui_turn(2); fx_ui_knob(170); fx_ui_knob(30);
    shot(d, "5_speed_reverse_live", 30);
    fx_ui_turn(3); fx_ui_knob(30); fx_ui_knob(140);
    shot(d, "6_chaos_live", 140);
    fx_ui_turn(2); fx_ui_foot_press();
    shot(d, "7_frozen_loop_length", 140);
    fx_ui_turn(1); fx_ui_knob(140); fx_ui_knob(90);
    shot(d, "8_frozen_loop_speed_live", 90);
    return 0;
}
