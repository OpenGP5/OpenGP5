#ifndef GP5_LED_PWM_H
#define GP5_LED_PWM_H
#include <stdint.h>

/* Three RGB LEDs share these active-high port-A colour lines. No hardware PWM is
 * available on A29..31. 32 slots at 50 us give a 625 Hz brightness carrier. */
#define LED_B (1u << 29)
#define LED_R (1u << 30)
#define LED_G (1u << 31)
#define LED_PINS (LED_R | LED_G | LED_B)
#define LED_PWM_STEPS 32u
#define LED_PWM_TICK_US 50u
#define LED_RGB(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))
#define LED_OFF_RED LED_RGB(1, 0, 0)     /* 3.125% red */
/* ui.c C_ON = sRGB (46,230,166), linearised and rounded to 32 slots. Physical
 * colour matching still needs a look at the pedal: LED efficiencies differ. */
#define LED_MINT LED_RGB(1, 25, 12)

typedef struct {
    uint32_t colour;
    unsigned phase;
} led_pwm_t;

/* Called only by the timer ISR. Latch all channels together at a frame boundary
 * so a state change cannot splice two colours into the same brightness frame. */
static inline uint32_t led_pwm_next(led_pwm_t *p, unsigned effect_on)
{
    uint32_t pins = 0;
    if (!p->phase) p->colour = effect_on ? LED_MINT : LED_OFF_RED;
    if (p->phase < (p->colour & 255u)) pins |= LED_R;
    if (p->phase < ((p->colour >> 8) & 255u)) pins |= LED_G;
    if (p->phase < ((p->colour >> 16) & 255u)) pins |= LED_B;
    p->phase = (p->phase + 1u) & (LED_PWM_STEPS - 1u);
    return pins;
}
#endif
