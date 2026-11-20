/* The controls and the screen: runs on the control processor (opengp5/platform/nds32/gp5_firmware.h). The screen is
 * the UI kit's settings page (gp5_ui.h, opengp5/docs/platform/ui.md):
 *
 *   turn PARA      pick a row: DRIVE, TONE, LEVEL, PRESET
 *   click PARA     adjust the row picked (turn to change, click again when done); on PRESET, turning loads presets
 *   hold PARA      save the sound into the preset shown
 *   foot switch    the effect on / off (LEDs mint / dim red)
 *   volume knob    output level */
#include "gp5_firmware.h"
#include "gp5_params.h"
#include "gp5_ui.h"
#include "fonts/sans_16.h"

static const uint8_t rows[] = { GP5_P_DRIVE, GP5_P_TONE, GP5_P_LEVEL, GP5_UI_PRESETS };   /* (ON is the foot switch) */
static gp5_ui_page_t page = { .rows = rows, .n = 4, .font = &gp5_font_sans_16, .row_h = 20, .upper = 1 };

static void show_on(void)
{
    gp5_led(gp5_param(GP5_P_ON) ? GP5_LED_ON : GP5_LED_BYPASS);
    page.faded = !gp5_param(GP5_P_ON);
}

void fw_init(void) { show_on(); gp5_log("%s ready", fw_name()); }
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
