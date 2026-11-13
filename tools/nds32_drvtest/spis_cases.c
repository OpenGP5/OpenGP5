/* SPI slave + DMA scenarios for the inter-chip audio stream: the calls the stock app makes
 * (init at stock 0x496c8, per-block restart in its DMA-done callback 0x123e0), with its arguments.
 * Builds against the vendor library or (-DOURS) against platform/nds32/hal/spis.c.
 * DMA buffers sit at fixed addresses so both builds write the same pointers. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x40004000u, 0x40004040u }, { 0x4000d000u, 0x4000d200u }, { 0x400100e0u, 0x40010100u } };
const unsigned n_windows = 3;
#define REG(a) (*(volatile uint32_t *)(a))
#define OUT(i, v) (REG(0x400FFF00u + 4u * (i)) = (uint32_t)(v))
#define RXC ((uint8_t *)0x00200000u)   /* circular rx (SPIS_DMA_Init) */
#define TXC ((uint8_t *)0x00200800u)
#define RXB ((uint8_t *)0x00201000u)   /* block buffers (StartSendRecv) */
#define TXB ((uint8_t *)0x00201200u)

void NVIC_SetPriority(int n, uint32_t p) { OUT(8, n); OUT(9, p); }
void NVIC_EnableIRQ(int n) { OUT(10, n + 1); }
#ifdef OURS
#include "hal/spis.h"
static void s_init(void) { gp5_spis_init(); NVIC_SetPriority(GP5_IRQ_SPIS_DMA, 0); NVIC_EnableIRQ(GP5_IRQ_SPIS_DMA); }
static void s_start(void) { gp5_spis_start(TXB, RXB, 0x200); }
static void s_state(void) { OUT(0, gp5_spis_done()); }
static void s_rearm(void) { gp5_spis_rearm(TXB, RXB, 0x200); }
#else
#include "type.h"
#include "spis.h"
#include "dma.h"
static uint8_t map[] = { 4, 5, 0xff, 0xff, 0xff, 7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 6, 0xff, 0xff, 0xff,
                         0xff, 0, 1, 2, 3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 };
static void cb(void) {}
const uint32_t MPUConfigTable[16] = { 0 };   /* pulled in via dma.o -> core_d1088.o, unused here */
static void s_init(void)
{
    DMA_ChannelAllocTableSet(map);
    SPIS_IoConfig(1);
    SPIS_Init(0);
    DMA_ChannelDisable(PERIPHERAL_ID_SPIS_RX);
    DMA_ChannelDisable(PERIPHERAL_ID_SPIS_TX);
    SPIS_DMA_Init(RXC, 0x200, TXC, 0x200);
    SPIS_BlockDMA_Init();
    SPIS_RxDmaModeSet(1);
    NVIC_SetPriority(13, 0);
    DMA_InterruptFunSet(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT, cb);
    DMA_InterruptEnable(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT, 1);
    SPIS_ClrRxIntFlag(); SPIS_ClrRxOverrunFlag();
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT);
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_TX, DMA_DONE_INT);
    SPIS_ClrRxFIFO(); SPIS_ClrTxFIFO();
    DMA_CircularFIFOClear(PERIPHERAL_ID_SPIS_RX);
    DMA_CircularFIFOClear(PERIPHERAL_ID_SPIS_TX);
    DMA_ChannelEnable(PERIPHERAL_ID_SPIS_RX);
    DMA_ChannelEnable(PERIPHERAL_ID_SPIS_TX);
}
static void s_start(void) { SPIS_DMA_StartSendRecv(TXB, RXB, 0x200); }
static void s_state(void) { OUT(0, SPIS_DMA_SendRecvState()); }
static void s_rearm(void)
{
    SPIS_ClrRxIntFlag(); SPIS_ClrRxOverrunFlag();
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT);
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_TX, DMA_DONE_INT);
    SPIS_DMA_StartSendRecv(TXB, RXB, 0x200);
}
#endif
static void init_start(void) { s_init(); s_start(); }
static void init_start_state(void) { s_init(); s_start(); s_state(); }
static void init_start_done_rearm(void) { s_init(); s_start(); REG(0x4000d09cu) |= 1u; REG(0x4000d0bcu) |= 1u; s_state(); s_rearm(); }
const scenario_t scenarios[] = { { "init", s_init }, { "init+start", init_start }, { "state", s_state },
                                 { "init+start+state", init_start_state }, { "init+start+done+rearm", init_start_done_rearm } };
const unsigned n_scenarios = 5;
