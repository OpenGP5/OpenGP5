/* Tremolo, control half: the screen and the controls (OpenGP5 1.0: platform/nds32/gp5_firmware.h). The example
 * firmware: everything it changes is a parameter (../params.h), so the platform sends it to the DSP, saves it,
 * keeps presets, and a computer can read and set it. The screen is the UI kit's settings page (gp5_ui.h).
 *
 *   turn PARA      pick a row: RATE, DEPTH, SHAPE, PRESET
 *   click PARA     adjust the row picked (turn to change; click again when done). On PRESET, turning loads presets
 *   hold PARA      save the sound into the preset shown
 *   foot switch    tremolo on / off (LED mint / dim red; the values turn grey while it is off)
 *   volume knob    output level (the platform's default) */
#include "gp5_firmware.h"
#include "gp5_params.h"
#include "gp5_ui.h"
#include "fonts/sans_16.h"

static const uint8_t rows[] = { GP5_P_RATE, GP5_P_DEPTH, GP5_P_SHAPE, GP5_UI_PRESETS };
static const int16_t steps[] = { 10, 5, 0, 0 };                  /* 0.1 Hz and 5 % a click */
static gp5_ui_page_t page = { .rows = rows, .n = 4, .steps = steps, .font = &gp5_font_sans_16, .row_h = 20, .upper = 1 };

static void show_on(void)
{
    gp5_led(gp5_param(GP5_P_ON) ? GP5_LED_ON : GP5_LED_BYPASS);
    page.faded = !gp5_param(GP5_P_ON);
}

void fw_init(void) { show_on(); }
void fw_foot_down(void) { gp5_param_set(GP5_P_ON, !gp5_param(GP5_P_ON)); show_on(); }
void fw_param_changed(unsigned id) { if (id == GP5_P_ON) show_on(); }        /* from a computer */
void fw_para_turn(int d) { gp5_ui_page_turn(&page, d); }
void fw_para_click(void) { gp5_ui_page_click(&page); }
void fw_para_hold(void)
{
    char m[12] = "Saved P";
    unsigned n = gp5_ui_page_preset(&page);
    if (!gp5_preset_save(n)) return;
    m[7] = (char)('1' + n);
    gp5_ui_page_message(&page, m, 1500);
}
int fw_draw(int force) { return gp5_ui_page_draw(&page, force); }

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
