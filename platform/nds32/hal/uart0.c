#include "uart0.h"

#define UART  0x40005000u
#define W(a)  (*(volatile uint32_t *)(a))
#define B(a)  (*(volatile uint8_t *)(a))
#define FRAME (UART + 0x00)    /* byte: bits 0-1 data length - 5, bit 2 parity on, bit 4 two stop bits, bit 5 on */
#define BAUD  (UART + 0x04)    /* bits 0-15: clock / (8 * baud) - 1, bits 16-18: eighths */
#define RXDAT (UART + 0x08)
#define RXCTL (UART + 0x0C)    /* byte: bit 0 receiver on, bit 1 interrupt enable, bit 2 clear interrupt, bit 4 clear errors */
#define STAT  (UART + 0x14)    /* bits 0-5 receive status, bit 8 transmit interrupt, bit 9 room in the transmitter */
#define TXDAT (UART + 0x18)
#define TXCTL (UART + 0x1C)    /* byte: bit 0 transmitter on, bit 2 clear interrupt */
#define FIFO  (UART + 0x20)    /* byte: bit 1 clear receive FIFO, bits 2-3 receive interrupt threshold */
#define FSTAT (UART + 0x24)    /* bit 0 receive FIFO empty, bit 2 transmitter empty */
#define MISC  (UART + 0x28)
#define CLKDIV  0x40021004u    /* UART clock = source / (this + 1) */
#define CLKGATE 0x40021030u    /* bit 15: UART0 baud clock gate */
#define RST1    0x40022004u    /* bit 9: UART0 reset line, low = in reset */

int gp5_uart0_init(uint32_t baud, uint32_t clock_hz)
{
    uint32_t clk, d = baud * 8u, q, r;

    B(RXCTL) &= (uint8_t)~0x01u;                 /* receiver and transmitter off while we change things */
    B(TXCTL) &= (uint8_t)~0x01u;
    W(CLKGATE) &= ~(1u << 15);
    B(FRAME) = (uint8_t)((B(FRAME) & ~0x03u) | 0x03u);   /* 8 data bits */
    B(FRAME) &= (uint8_t)~0x04u;                 /* no parity */
    B(FRAME) &= (uint8_t)~0x10u;                 /* 1 stop bit */
    B(FRAME) |= 0x20u;
    clk = clock_hz / (W(CLKDIV) + 1u);
    q = clk / d; r = clk % d;
    W(BAUD) = ((q - 1u) & 0xFFFFu) | ((((r << 3) / d) & 7u) << 16);
    W(CLKGATE) |= 1u << 15;
    B(RXCTL) |= 0x01u;                           /* receiver on, its interrupt off, flags left alone */
    B(RXCTL) &= (uint8_t)~0x02u;
    B(RXCTL) &= (uint8_t)~0x08u;
    B(RXCTL) &= (uint8_t)~0x20u;
    B(RXCTL) &= 0x7Fu;
    B(TXCTL) |= 0x01u;                           /* transmitter on, its interrupt off */
    B(TXCTL) &= (uint8_t)~0x02u;
    B(TXCTL) &= (uint8_t)~0x08u;
    B(FIFO) &= (uint8_t)~0x0Cu;
    B(MISC) &= (uint8_t)~0x1Fu;
    W(RST1) &= ~(1u << 9);                       /* pulse the module's reset line */
    W(RST1) |= 1u << 9;
    return 1;
}

void gp5_uart0_rx_fifo_clear(void) { B(FIFO) |= 0x02u; }
void gp5_uart0_rx_int_from_first_byte(void) { B(FIFO) &= (uint8_t)~0x0Cu; }
void gp5_uart0_rx_int_enable(int on) { B(RXCTL) = (uint8_t)((B(RXCTL) & ~0x02u) | (on ? 0x02u : 0u)); }
uint32_t gp5_uart0_rx_status(void) { return W(STAT) & 0x3Fu; }
void gp5_uart0_rx_int_clear(void) { B(RXCTL) |= 0x04u; }
void gp5_uart0_rx_err_clear(void) { B(RXCTL) |= 0x10u; }
int gp5_uart0_tx_int_pending(void) { return (int)((W(STAT) >> 8) & 1u); }
void gp5_uart0_tx_int_clear(void) { B(TXCTL) |= 0x04u; }

int gp5_uart0_recv_byte(uint8_t *b)
{
    B(RXCTL) &= 0x7Fu;
    if (W(FSTAT) & 1u) return 0;
    *b = (uint8_t)W(RXDAT);
    return 1;
}

uint32_t gp5_uart0_send(const uint8_t *buf, uint32_t n, uint32_t timeout_ms)
{
    uint32_t t0 = gp5_ms(), i;

    B(TXCTL) &= (uint8_t)~0x08u;
    for (i = 0; i < n; i++) {
        while (!(W(STAT) & (1u << 9)) && (uint32_t)(gp5_ms() - t0) < timeout_ms) {}
        W(TXDAT) = buf[i];
        W(0x40030000u) = 0;                      /* as the vendor library does after every byte; purpose not known */
    }
    while (!(W(FSTAT) & (1u << 2)) && (uint32_t)(gp5_ms() - t0) < timeout_ms) {}
    return n;
}
