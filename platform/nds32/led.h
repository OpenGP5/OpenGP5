#ifndef GP5_LED_H
#define GP5_LED_H
#include "led_pwm.h"

/* Request a colour without touching hardware. Start only after UI/DSP startup. */
void led_set_effect(unsigned on);
void led_start(void);

/* Debugger status: 0 = not started, 1 = running, 2 = disabled after a fault. */
extern volatile unsigned gp5_led_status;
#endif
