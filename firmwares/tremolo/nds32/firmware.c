/* Tremolo, control half: the screen and the controls (OpenGP5 1.0: platform/nds32/gp5_firmware.h). The example
 * firmware: everything it changes is a parameter (../params.h), so the platform sends it to the DSP, saves it,
 * keeps presets, and a computer can read and set it.
 *
 *   turn PARA      change the selected row: RATE, DEPTH, SHAPE, or which PRESET (loads it if it holds a sound)
 *   click PARA     select the next row
 *   hold PARA      save the sound into the preset shown
 *   foot switch    tremolo on / off (LED mint / dim red)
 *   volume knob    output level (the platform's default)
 *
 * Screen: four rows in the platform's smooth font, the selected one highlighted; "*" after the preset when the
 * sound differs from it. Only rows that changed are drawn again. */
#include "gp5_firmware.h"
#include "gp5_params.h"
#include "gp5_font.h"
#include "fonts/sans_16.h"

#define ROWS 4
#define ROW_H 20
#define MARGIN 6
static const char *const label[ROWS] = { "RATE", "DEPTH", "SHAPE", "PRESET" };
static unsigned row, preset_pick, saved_flash;    /* the selected row; the preset shown; ticks to show "saved" */
static unsigned ticks;

static void show_on(void) { gp5_led(gp5_param(GP5_P_ON) ? GP5_LED_ON : GP5_LED_BYPASS); }

void fw_init(void)
{
    int c = gp5_preset_current();
    row = 0; preset_pick = c < 0 ? 0u : (unsigned)c; saved_flash = 0;
    show_on();
}
void fw_foot_down(void) { gp5_param_set(GP5_P_ON, !gp5_param(GP5_P_ON)); show_on(); }
void fw_param_changed(unsigned id) { if (id == GP5_P_ON) show_on(); }        /* from a computer */
void fw_para_click(void) { row = (row + 1u) % ROWS; }
void fw_para_hold(void) { if (gp5_preset_save(preset_pick)) saved_flash = 100u; }
void fw_para_turn(int d)
{
    if (row == 0) gp5_param_set(GP5_P_RATE, gp5_param(GP5_P_RATE) + 10 * d);     /* 0.1 Hz a click */
    if (row == 1) gp5_param_set(GP5_P_DEPTH, gp5_param(GP5_P_DEPTH) + 5 * d);
    if (row == 2) gp5_param_set(GP5_P_SHAPE, (gp5_param(GP5_P_SHAPE) + 3 + d % 3) % 3);
    if (row == 3) {
        preset_pick = (preset_pick + gp5_preset_count() + (unsigned)(d % (int)gp5_preset_count())) % gp5_preset_count();
        gp5_preset_load(preset_pick);                                             /* (an empty one: nothing changes) */
    }
}
void fw_tick(void) { ticks++; if (saved_flash) saved_flash--; }

/* "5.00 Hz" for RATE, here and on a computer */
int fw_param_text(unsigned id, int32_t v, char *t)
{
    unsigned n = 0;
    if (id != GP5_P_RATE) return 0;
    if (v >= 1000) t[n++] = (char)('0' + v / 1000);
    t[n++] = (char)('0' + v / 100 % 10); t[n++] = '.'; t[n++] = (char)('0' + v / 10 % 10); t[n++] = (char)('0' + v % 10);
    t[n++] = ' '; t[n++] = 'H'; t[n++] = 'z'; t[n] = 0;
    return 1;
}

static void value_text(unsigned r, char *t)
{
    static const char *const shapes[3] = { "Sine", "Triangle", "Square" };
    int32_t v;
    unsigned n = 0, i;
    if (r == 0) { fw_param_text(GP5_P_RATE, gp5_param(GP5_P_RATE), t); return; }
    if (r == 1) { v = gp5_param(GP5_P_DEPTH); if (v >= 100) t[n++] = '1'; if (v >= 10) t[n++] = (char)('0' + v / 10 % 10); t[n++] = (char)('0' + v % 10); t[n++] = ' '; t[n++] = '%'; t[n] = 0; return; }
    if (r == 2) { for (i = 0; shapes[gp5_param(GP5_P_SHAPE)][i]; i++) t[n++] = shapes[gp5_param(GP5_P_SHAPE)][i]; t[n] = 0; return; }
    if (saved_flash) { const char *s = "saved"; for (i = 0; s[i]; i++) t[n++] = s[i]; t[n] = 0; return; }
    t[n++] = 'P'; t[n++] = (char)('1' + preset_pick);
    if (!gp5_preset_used(preset_pick)) { const char *s = " empty"; for (i = 0; s[i]; i++) t[n++] = s[i]; }
    else if ((int)preset_pick == gp5_preset_current() && gp5_preset_dirty()) t[n++] = '*';
    t[n] = 0;
}

int fw_draw(int force)
{
    static uint16_t band[GP5_SCREEN_W * ROW_H];
    static char shown[ROWS][16];
    static unsigned shown_sel[ROWS] = { 9, 9, 9, 9 }, shown_on[ROWS] = { 9, 9, 9, 9 };   /* how each row was drawn */
    unsigned r, on = (unsigned)gp5_param(GP5_P_ON);
    for (r = 0; r < ROWS; r++) {
        char t[16];
        unsigned k, same = 1, sel = r == row;
        value_text(r, t);
        for (k = 0; k < 16 && (t[k] || shown[r][k]); k++) if (t[k] != shown[r][k]) same = 0;
        if (!force && same && sel == shown_sel[r] && on == shown_on[r]) continue;
        {   /* this row: highlighted when selected, dimmer while the tremolo is off */
            uint16_t bg = gp5_color(sel ? 0x2A9D8F : 0x101418);
            uint32_t ink = on ? 0xF2F4F7 : 0x7A8290;
            for (k = 0; k < GP5_SCREEN_W * ROW_H; k++) band[k] = bg;
            gp5_draw_text(band, GP5_SCREEN_W, ROW_H, MARGIN, 2, &gp5_font_sans_16, label[r], sel ? 0xFFFFFF : 0x8A93A3);
            gp5_draw_text(band, GP5_SCREEN_W, ROW_H, GP5_SCREEN_W - MARGIN - gp5_text_width(&gp5_font_sans_16, t), 2,
                          &gp5_font_sans_16, t, sel ? 0xFFFFFF : ink);
            gp5_blit(0, r * ROW_H, GP5_SCREEN_W, ROW_H, band);
        }
        for (k = 0; k < 16; k++) shown[r][k] = t[k];
        shown_sel[r] = sel; shown_on[r] = on;
        return 1;                                    /* one row per pass: the next on the next one */
    }
    return 0;
}
