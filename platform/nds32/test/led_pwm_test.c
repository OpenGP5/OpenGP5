#include <assert.h>
#include <stdio.h>
#include "../led_pwm.h"
#include "fx_ui.h"

static void check_frame(led_pwm_t *p, unsigned on)
{
    unsigned i, red = 0, green = 0, blue = 0;
    assert(p->phase == 0);
    for (i = 0; i < LED_PWM_STEPS; i++) {
        uint32_t pins = led_pwm_next(p, on);
        assert(!(pins & ~LED_PINS));
        red += !!(pins & LED_R);
        green += !!(pins & LED_G);
        blue += !!(pins & LED_B);
    }
    assert(red == 1);
    assert(green == (on ? 25u : 0u));
    assert(blue == (on ? 12u : 0u));
    assert(p->phase == 0);
}

int main(void)
{
    led_pwm_t p = {0};
    unsigned on, change_at, i;
    assert(LED_PWM_STEPS * LED_PWM_TICK_US == 1600u);

    /* Every frame remains lit at the same brightness; no slow heartbeat or fade. */
    for (on = 0; on < 2; on++)
        for (i = 0; i < 1000; i++) check_frame(&p, on);

    /* Change state at every possible point in a frame: no mixed frame, and the
     * complete new colour arrives at the next boundary (within 1.6 ms). */
    for (on = 0; on < 2; on++) {
        for (change_at = 1; change_at < LED_PWM_STEPS; change_at++) {
            led_pwm_t changed = {0}, reference = {0};
            for (i = 0; i < LED_PWM_STEPS; i++) {
                uint32_t expected = led_pwm_next(&reference, on);
                assert(led_pwm_next(&changed, i < change_at ? on : !on) == expected);
            }
            check_frame(&changed, !on);
        }
    }

    /* Real pedal controls: off -> on -> frozen -> released -> off; freezing
     * from off also turns the effect on. Frozen stays mint. */
    fx_ui_init();
    assert(!fx_ui_on()); check_frame(&p, fx_ui_on());
    fx_ui_para_push();
    assert(fx_ui_on()); check_frame(&p, fx_ui_on());
    fx_ui_foot_press();
    assert(fx_ui_on() && fx_ui_frozen()); check_frame(&p, fx_ui_on());
    fx_ui_foot_press();
    assert(fx_ui_on() && !fx_ui_frozen()); check_frame(&p, fx_ui_on());
    fx_ui_para_push();
    assert(!fx_ui_on()); check_frame(&p, fx_ui_on());
    fx_ui_foot_press();
    assert(fx_ui_on() && fx_ui_frozen()); check_frame(&p, fx_ui_on());
    fx_ui_para_push();
    assert(!fx_ui_on() && !fx_ui_frozen()); check_frame(&p, fx_ui_on());
    puts("ALL OK: steady red/mint PWM, frame-boundary changes, effect/freeze controls");
    return 0;
}
