#include "spis.h"
/* Register behaviour observed in the instruction simulator (tools/nds32_drvtest/spis_cases.c);
 * the call sequence is the stock app's (init 0x496c8, per-block restart 0x123e0). */

#define SPIS      0x40004000u
#define SPIS_CTRL (SPIS + 0x00)        /* bits 0..2 clock mode, 3/6/7 set by init, 5/6 DMA requests on */
#define SPIS_CLR  (SPIS + 0x08)        /* write-one: 0 rx interrupt, 1 rx FIFO, 2 tx FIFO, 3 rx overrun */
#define SPIS_TXD  (SPIS + 0x0C)
#define SPIS_RXD  (SPIS + 0x10)
#define DMA       0x4000D000u
#define CH(n)     (DMA + 0x20u * (n))  /* +0 config, +4 length, +0x10 control, +0x14 source, +0x18 destination, +0x1C status */
#define DMA_EN    (DMA + 0x100)        /* one bit per channel */
#define CH_RX     4
#define CH_TX     5
#define PIN_MUX   0x400100F0u          /* A20..A23 in the top half */
#define W(a)      (*(volatile uint32_t *)(a))

static uint32_t mux_before;                         /* A20..A23 as they were: A22 is a board control output */
static int mux_saved;

void gp5_spis_init(void)
{
    W(DMA_EN) &= ~((1u << CH_RX) | (1u << CH_TX));
    if (!mux_saved) { mux_before = W(PIN_MUX); mux_saved = 1; }
    W(PIN_MUX) = (W(PIN_MUX) & 0x0000FFFFu) | 0x18120000u;
    W(SPIS_CTRL) = (W(SPIS_CTRL) & ~0x7u) | 0xC8u;
    W(CH(CH_RX) + 0x00) = 0x00444000u;               /* peripheral -> memory */
    W(CH(CH_RX) + 0x04) = 0; W(CH(CH_RX) + 0x08) = 0; W(CH(CH_RX) + 0x0C) = 0;
    W(CH(CH_RX) + 0x10) = 0x00010000u;
    W(CH(CH_RX) + 0x14) = SPIS_RXD;
    W(CH(CH_RX) + 0x18) = 0;
    W(CH(CH_RX) + 0x1C) = 0x100u;                    /* clear done */
    W(CH(CH_TX) + 0x00) = 0x00401021u;               /* memory -> peripheral */
    W(CH(CH_TX) + 0x04) = 0; W(CH(CH_TX) + 0x08) = 0; W(CH(CH_TX) + 0x0C) = 0;
    W(CH(CH_TX) + 0x10) = 0x00010000u;
    W(CH(CH_TX) + 0x14) = 0;
    W(CH(CH_TX) + 0x18) = SPIS_TXD;
    W(CH(CH_TX) + 0x1C) = 0x100u;
    W(SPIS_CLR) |= 0xFu;
    W(DMA_EN) |= (1u << CH_RX) | (1u << CH_TX);
}

/* A channel only (re)starts when its enable bit goes from 0 to 1: switch each one off before giving it a new buffer
 * and on again afterwards (the vendor's DMA_BlockBufSet + DMA_ChannelEnable order; the hardware showed it). Writing
 * the buffer into a channel that stays enabled leaves it stopped. */
void gp5_spis_start(const void *tx, void *rx, uint32_t n)
{
    W(SPIS_CLR) |= 0x2u;                             /* empty both FIFOs */
    W(SPIS_CLR) |= 0x4u;
    W(DMA_EN) &= ~(1u << CH_TX);
    W(CH(CH_TX) + 0x14) = (uint32_t)tx; W(CH(CH_TX) + 0x04) = n;
    W(DMA_EN) &= ~(1u << CH_RX);
    W(CH(CH_RX) + 0x18) = (uint32_t)rx; W(CH(CH_RX) + 0x04) = n;
    W(DMA_EN) |= 1u << CH_TX;
    W(DMA_EN) |= 1u << CH_RX;
    W(SPIS_CTRL) |= 0x40u;                           /* slave DMA requests on */
    W(SPIS_CTRL) |= 0x20u;
}

int gp5_spis_done(void)
{
    if (!(W(CH(CH_RX) + 0x1C) & 1u) || !(W(CH(CH_TX) + 0x1C) & 1u)) return 0;
    W(SPIS_CTRL) &= ~0x60u;
    return 1;
}

void gp5_spis_rearm(const void *tx, void *rx, uint32_t n)
{
    W(SPIS_CLR) |= 0x9u;                             /* rx interrupt + overrun flags */
    W(CH(CH_RX) + 0x1C) |= 0x100u;
    W(CH(CH_TX) + 0x1C) |= 0x100u;
    gp5_spis_start(tx, rx, n);
}

/* Stop the slave (DMA requests off, both channels off) and give A20..A23 back their earlier function: the factory
 * loader needs A22 as the board control output it was, or its update session gives up. */
void gp5_spis_release(void)
{
    W(SPIS_CTRL) &= ~0x60u;
    W(DMA_EN) &= ~((1u << CH_RX) | (1u << CH_TX));
    if (mux_saved) W(PIN_MUX) = mux_before;
}
