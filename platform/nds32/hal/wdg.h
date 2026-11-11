#ifndef GP5_HAL_WDG_H
#define GP5_HAL_WDG_H
/* Watchdog of the BP1048, our own driver (see hal/gpio.h for how these are written and checked). */
void gp5_wdg_disable(void);
void gp5_wdg_enable_1s(void);   /* resets the chip about 1 s later unless disabled again */
#endif
