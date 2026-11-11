#ifndef GP5_HAL_UART0_H
#define GP5_HAL_UART0_H
#include <stdint.h>
/* UART0 of the BP1048 (the link to the DSP), our own driver: 8 data bits, no parity, 1 stop bit.
 * Written from the vendor library's observed register behaviour and checked by difference in the
 * simulator (tools/nds32_drvtest/uart_cases.c). */

/* clock_hz: the frequency of the clock the UART is switched to (the platform selects the 240 MHz APLL).
 * The UART's own clock divider (register 0x40021004) is taken into account here. Returns 1. */
int gp5_uart0_init(uint32_t baud, uint32_t clock_hz);

void gp5_uart0_rx_fifo_clear(void);
void gp5_uart0_rx_int_from_first_byte(void);     /* receive interrupt threshold: 1 byte */
void gp5_uart0_rx_int_enable(int on);
uint32_t gp5_uart0_rx_status(void);              /* bit 0 = receive interrupt, bits 2..5 = error flags */
void gp5_uart0_rx_int_clear(void);
void gp5_uart0_rx_err_clear(void);
int gp5_uart0_tx_int_pending(void);
void gp5_uart0_tx_int_clear(void);
int gp5_uart0_recv_byte(uint8_t *b);             /* 1 = a byte was taken from the receive FIFO */
/* Sends n bytes; gives up waiting for the transmitter after timeout_ms in total. Returns n. */
uint32_t gp5_uart0_send(const uint8_t *buf, uint32_t n, uint32_t timeout_ms);

/* provided by the platform: milliseconds since start-up (used for the send timeout) */
extern uint32_t gp5_ms(void);
#endif
