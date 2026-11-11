#ifndef GP5_CLOCK_H
#define GP5_CLOCK_H
#include <stdint.h>

void gp5_chip_init(void);         /* analogue / pad set-up and the factory clock trim (first thing in main) */
void gp5_clock_init(void);        /* 24 MHz crystal, PLL 288 MHz system clock, APLL 240 MHz for USB + UART */
void gp5_chip_mem_init(void);     /* memory set-up register (start-up, before .data / .bss) */
void gp5_chip_reset_mask(void);   /* before the hand-over to the factory loader */
uint8_t gp5_efuse_read(unsigned addr);

#endif
