#include <stdint.h>
#include "backlight.h"

#define W(a)  (*(volatile uint32_t *)(a))
#define H(a)  (*(volatile uint16_t *)(a))
#define T5      0x4002F000u      /* timer 5 */
#define CTRL    (T5 + 0x00)
#define RELOAD  (T5 + 0x0C)      /* period - 1 */
#define PRESC   (T5 + 0x10)
#define OUTMODE (T5 + 0x1C)      /* 16-bit; bits 9-12: output type */
#define COMPARE (T5 + 0x24)      /* output is active while the counter is below this */
#define PIN_MUX 0x400100ECu      /* bits 8-11: function of A10 (5 = timer 5 output) */
#define PWM_PD  0x400100D0u      /* bit 2: pull-down-current drive for this output (off) */

void gp5_backlight_pin(int pwm)
{
    if (pwm) {
        W(PIN_MUX) &= ~0x00000F00u;
        W(PIN_MUX) |= 0x00000500u;
    } else {
        W(PIN_MUX) &= ~0x00000F00u;
        W(PWM_PD) &= ~0x00000004u;
    }
}

void gp5_backlight_timer_setup(void)
{
    W(CTRL) &= ~0x1u;
    W(CTRL) |= 0x1000u;
    W(CTRL) |= 0x2000u;
    W(CTRL) &= ~0x2u;
    W(CTRL) &= ~0x10u;                   /* count down */
    W(CTRL) &= ~0x60u;
    W(RELOAD) = 4000u - 1u;
    W(PRESC) = 0;
    W(CTRL) &= ~0x100u;
    W(CTRL) |= 0x8u;
    H(OUTMODE) = (uint16_t)((H(OUTMODE) & ~0x1E00u) | (6u << 9));   /* plain pulse-width output */
    W(COMPARE) = 0;                      /* 0 % */
    H(T5 + 0x04) &= (uint16_t)~0x07u;
    H(T5 + 0x04) &= (uint16_t)~0x08u;
    H(T5 + 0x04) &= (uint16_t)~0x30u;
    H(T5 + 0x04) &= (uint16_t)~0x40u;
    W(CTRL) |= 0x1u;
}

void gp5_backlight_timer_start(void)
{
    W(CTRL) |= 0x1u;
    W(CTRL) |= 1u << 24;                 /* load the new values */
}

void gp5_backlight_duty(unsigned percent)
{
    uint32_t ctrl = W(CTRL), period = W(RELOAD), cmp;

    if (!(ctrl & 0x60u)) period += 1u;
    if ((ctrl & 0x70u) == 0x10u && percent >= 100u) cmp = period + 1u;
    else cmp = (uint32_t)(percent * period) / 100u;
    if (!(ctrl & 0x70u) && cmp) cmp -= 1u;
    W(COMPARE) = cmp;
}
