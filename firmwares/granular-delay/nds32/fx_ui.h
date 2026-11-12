#ifndef GP5_FX_UI_H
#define GP5_FX_UI_H
#include <stdint.h>

/* On-pedal control of the DSP's granular delay + micro-looper (controls: firmwares/granular-delay/README.md). No hardware in
 * here: the main loop feeds knob and switch events in and sends what fx_ui_take() hands out to the
 * DSP as link command 0x7F10. Also built on the host (fw-build/nds32_app/test/fx_ui_test.c).
 *
 *   turn PARA      choose the setting (MIX, DELAY, GRAIN, SPEED, SPRAY, FEEDBK, CHAOS, CLOCK, LOOP,
 *                  LP SPD, LP LVL, LEVEL)
 *   volume knob    changes the chosen setting over its whole range - but only once the knob is
 *                  actually turned. Choosing another setting never changes anything by itself: the
 *                  setting keeps its value until the knob moves, then it follows the knob.
 *   press Foot     freeze / unfreeze the loop (switches the effect on if it was off)
 *   push PARA      effect on / off (off also lets go of the loop)
 */
#define FX_COLS 13

void fx_ui_init(void);
void fx_ui_turn(int detents);
void fx_ui_foot_press(void);
void fx_ui_para_push(void);
void fx_ui_knob(unsigned knob);                  /* 0..255; call with every reading */
unsigned fx_ui_live(void);                       /* 1 = the chosen setting is following the knob */
void fx_ui_resend_all(void);                     /* e.g. after the DSP has (re)started */
/* Next setting that has to go to the DSP: returns 1 and fills id / value, or 0 when nothing is waiting. */
int fx_ui_take(uint32_t *id, uint32_t *value);
unsigned fx_ui_on(void);
unsigned fx_ui_frozen(void);
unsigned fx_ui_level(void);                      /* output level 0..255, for dsp_ctl_poll() */
void fx_ui_text(char *line);                     /* chosen setting, FX_COLS characters + 0: "DELAY   300MS" */
void fx_ui_status(char *line);                   /* "MIX 50 ON  FR" */
/* for the graphic screen (ui.c) */
#define FX_UI_COUNT 12
const char *fx_ui_name(void);                    /* chosen setting's name */
void fx_ui_value(char *v);                       /* its value as text, up to 6 characters + 0 */
unsigned fx_ui_frac(void);                       /* its position in its range, 0..255 */
unsigned fx_ui_sel(void);                        /* its index, 0..FX_UI_COUNT-1 */
#endif
