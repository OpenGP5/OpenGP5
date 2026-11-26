/* The Bluetooth radio's start-up (radio.c). */
#ifndef GP5_BT_RADIO_H
#define GP5_BT_RADIO_H
#include <stdint.h>
void     rf_wr(unsigned n, uint16_t v);
uint16_t rf_rd(unsigned n);
int      radio_start(void);
extern uint32_t rf_diag_step, rf_diag_val;   /* 0, or -1 (oscillator didn't settle), -2 (frequency set-up didn't finish) */
#endif
