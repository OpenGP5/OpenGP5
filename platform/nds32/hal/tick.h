#ifndef GP5_HAL_TICK_H
#define GP5_HAL_TICK_H
#include <stdint.h>
/* The 1 ms tick (timer 1), our own code. gp5_tick_setup() programs and starts the timer; the caller
 * then enables interrupts and the timer-1 interrupt line. The interrupt handler is in tick.c. */
void gp5_tick_setup(void);
uint32_t gp5_ms(void);                     /* milliseconds since gp5_tick_setup() */

/* A deadline, for "has this taken too long". Layout as the vendor library's (see tick.c). */
typedef struct { uint32_t deadline, start; uint8_t expired; } gp5_timeout_t;
void gp5_timeout_set(gp5_timeout_t *t, uint32_t ms);
int gp5_timeout_expired(gp5_timeout_t *t);
/* The same check with the vendor library's timing: the call that first sees the deadline passed still
 * returns 0, later calls 1. Kept for the USB code, whose waits were written against it. */
int gp5_timeout_late(gp5_timeout_t *t);

/* Busy-wait delays (run from RAM; loop counts as the vendor library's). */
void gp5_delay_us(uint32_t us);
void gp5_delay_ms(uint32_t ms);
#endif
