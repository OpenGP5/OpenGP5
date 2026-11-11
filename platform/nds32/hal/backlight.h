#ifndef GP5_HAL_BACKLIGHT_H
#define GP5_HAL_BACKLIGHT_H
/* The display backlight: timer 5 as a pulse-width output on pin A10, our own driver. Does what the
 * stock firmware's set-up does (period 4000 system-clock counts, counting down), written from the
 * vendor library's observed register behaviour and checked by difference in the simulator
 * (tools/nds32_drvtest/backlight_cases.c). */
void gp5_backlight_pin(int pwm);            /* 1: A10 is the timer's output, 0: A10 is a plain pin again */
void gp5_backlight_timer_setup(void);       /* leaves the output at 0 % */
void gp5_backlight_timer_start(void);
void gp5_backlight_duty(unsigned percent);  /* 0..100 */

/* the whole thing, in the order the stock firmware does it */
static inline void gp5_backlight_on(unsigned percent)
{
    gp5_backlight_pin(0);
    gp5_backlight_pin(1);
    gp5_backlight_timer_setup();
    gp5_backlight_timer_start();
    gp5_backlight_duty(percent);
}
#endif
