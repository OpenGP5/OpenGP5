/* GP-5 open platform, control processor side: what a firmware implements and what it may use.
 *
 * A firmware supplies the C files in firmwares/<name>/nds32/ (all are compiled in). One of them
 * defines the fw_* functions below. The platform owns start-up, the display driver, the inputs,
 * the LEDs, USB-MIDI, the DSP link and the hand-over to the factory loader; it calls the firmware
 * from its main loop, so none of these may block.
 *
 * Start-up: the platform shows its own text lines until the DSP answers and is in sync, then calls
 * fw_dsp_ready() and from then on fw_draw() every pass of the main loop. */
#ifndef GP5_FIRMWARE_H
#define GP5_FIRMWARE_H
#include <stdint.h>

#define GP5_SCREEN_W 160
#define GP5_SCREEN_H 80

/* ---- implemented by the firmware ---- */
void fw_init(void);
void fw_foot(void);                       /* foot switch pressed */
void fw_para_push(void);                  /* PARA knob pushed */
void fw_para_turn(int detents);           /* PARA knob turned, +/- clicks */
void fw_knob(unsigned knob);              /* volume knob, 0..255, about every 10 ms */
unsigned fw_level(void);                  /* output level for the DSP, 0..255 */
void fw_dsp_ready(void);                  /* the DSP (re)started with its defaults: queue every setting again */
int fw_take(uint32_t *id, uint32_t *value);   /* next setting for the DSP, sent as link command 0x7F10
                                                 [id u32][value u32]; return 0 when there is none */
unsigned fw_setting_count(void);          /* ids 0..count-1 (at most 32) must be acknowledged once before
                                             the platform starts the LEDs */
int fw_draw(unsigned knob, int force);    /* draw (part of) the screen; return non-zero while more is left
                                             for the next pass. force = redraw everything. */
unsigned fw_active(void);                 /* LED colour: 0 = dim red, 1 = mint, or GP5_LED(r, g, b) */

/* A firmware's own LED colour, returned by fw_active(): r, g, b = brightness 0..32 (32 = fully lit) on each of
 * the LEDs' three colour lines. LEDs are not linear and their colours differ in strength: check on the pedal. */
#define GP5_LED(r, g, b) (0x01000000u | (uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

/* ---- provided by the platform ---- */
/* Send w x h pixels (RGB565, big-endian bytes as the panel wants them) to the screen at x, y. */
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
/* One of five 13-character text lines in the platform's 5x7 font, white on black. */
void gp5_text(unsigned line, const char *s);
#endif
