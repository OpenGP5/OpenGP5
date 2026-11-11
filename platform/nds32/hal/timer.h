#ifndef GP5_HAL_TIMER_H
#define GP5_HAL_TIMER_H
#include <stdint.h>
/* The BP1048's general timers as plain periodic / one-shot timers, our own driver. Written from the
 * vendor library's observed register behaviour, checked by difference in the simulator
 * (tools/nds32_drvtest/timer_cases.c). Timers are numbered 1..6 as on the chip. */

/* The platform's clock set-up, needed to turn microseconds into timer counts: the PLL and APLL
 * frequencies it programmed. The dividers and the clock switch are read from the chip. */
#define GP5_PLL_HZ   288000000u
#define GP5_APLL_HZ  240000000u

/* Period in microseconds; single_shot = stop after one period. Leaves the timer stopped with its
 * period interrupt switched on. Returns 0, or -1 for a timer number that does not exist or when the
 * system clock is not running from a PLL. */
int gp5_timer_config(unsigned timer, uint32_t usec, int single_shot);
void gp5_timer_start(unsigned timer);
void gp5_timer_pause(unsigned timer, int pause);
void gp5_timer_int_clear(unsigned timer);      /* acknowledge the period interrupt */
uint32_t gp5_sys_clock_hz(void);               /* 0 when not running from a PLL */
#endif
