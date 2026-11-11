#ifndef GP5_UI_H
#define GP5_UI_H
#include <stdint.h>

/* The effect screen (160 x 80). No hardware in here: ui_draw() renders the screen in five horizontal
 * bands and hands each band whose content changed to ui_blit(), which the platform provides (the pedal:
 * set_window + SPI; the host: a picture, tools/ui_mock.py).
 *
 *   header   effect name, FRZ badge while the loop is frozen, ON / OFF badge
 *   name     the chosen setting
 *   value    its value, large; amber while it follows the big knob
 *   bar      where the value is in its range; a small marker shows where the big knob stands while the
 *            setting is still waiting for it
 *   footer   one dot per setting, the chosen one wider
 */
#define UI_W 160
#define UI_H 80

typedef struct {
    const char *name;      /* chosen setting, up to 6 characters */
    char value[8];         /* its value as text, up to 6 characters */
    uint8_t frac;          /* value position in its range, 0..255 */
    uint8_t knob;          /* big knob position, 0..255 */
    uint8_t live;          /* the setting follows the knob */
    uint8_t on, frozen;
    uint8_t sel, count;    /* chosen setting / number of settings */
} ui_state_t;

/* Draws at most ONE band per call, so the caller's loop is never held for long: returns 1 if it drew
 * one (call again), 0 when the screen is up to date. force = every band has to be redrawn.
 * Pixels are 16-bit in the platform's own format (UI_RGB in ui.c). */
int ui_draw(const ui_state_t *s, int force);
extern void ui_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
#endif
