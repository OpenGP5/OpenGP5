/* Granular delay / micro-looper: the control-processor half (platform interface: gp5_firmware.h).
 * Controls are in fx_ui.c, the screen in ui.c; this file only connects them to the platform. */
#include "gp5_firmware.h"
#include "fx_ui.h"
#include "ui.h"

void fw_init(void) { fx_ui_init(); }
void fw_foot(void) { fx_ui_foot_press(); }                 /* freeze / release the loop */
void fw_para_push(void) { fx_ui_para_push(); }             /* effect on / off */
void fw_para_turn(int detents) { fx_ui_turn(detents); }    /* choose the setting */
void fw_knob(unsigned knob) { fx_ui_knob(knob); }          /* change the chosen setting once turned */
unsigned fw_level(void) { return fx_ui_level(); }
void fw_dsp_ready(void) { fx_ui_resend_all(); }
int fw_take(uint32_t *id, uint32_t *value) { return fx_ui_take(id, value); }
unsigned fw_setting_count(void) { return 13; }             /* the DSP effect's 13 parameters */
unsigned fw_active(void) { return fx_ui_on(); }

int fw_draw(unsigned knob, int force)
{
    ui_state_t us;
    us.name = fx_ui_name();
    fx_ui_value(us.value);
    us.frac = (uint8_t)fx_ui_frac();
    us.knob = (uint8_t)knob;
    us.live = (uint8_t)fx_ui_live();
    us.on = (uint8_t)fx_ui_on();
    us.frozen = (uint8_t)fx_ui_frozen();
    us.sel = (uint8_t)fx_ui_sel();
    us.count = FX_UI_COUNT;
    return ui_draw(&us, force);
}

void ui_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px) { gp5_blit(x, y, w, h, px); }
