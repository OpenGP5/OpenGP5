/* UART0 scenarios: every UART0 call the platform makes, with its real arguments. Builds against the
 * vendor library or (-DOURS) against platform/nds32/hal/uart0.c. Values a call returns are stored in an
 * unused GPIO word (0x400101F0) so that they show in the log. Status bits that hardware sets by itself
 * (FIFO empty / not empty, room in the transmitter, transmitter empty) are put in place by the scenario. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x40005000u, 0x40005030u }, { 0x40021000u, 0x40021040u }, { 0x40022000u, 0x40022008u } };
const unsigned n_windows = 3;
#define REG(a) (*(volatile uint32_t *)(a))
#define OUT(v) (REG(0x400101F0u) = (uint32_t)(v))

#ifdef OURS
#include "hal/uart0.h"
uint32_t gp5_ms(void) { return 0; }
#define CLOCKS()       ((void)0)
#define INIT()         gp5_uart0_init(460800, 240000000u)
#define RXFIFO_CLR()   (gp5_uart0_rx_fifo_clear(), 0)
#define TRIG0()        (gp5_uart0_rx_int_from_first_byte(), 0)
#define RXINT(on)      (gp5_uart0_rx_int_enable(on), 0)
#define RXSTAT()       gp5_uart0_rx_status()
#define RXINT_CLR()    (gp5_uart0_rx_int_clear(), 0)
#define RXERR_CLR()    (gp5_uart0_rx_err_clear(), 0)
#define TXSTAT()       gp5_uart0_tx_int_pending()
#define TXINT_CLR()    (gp5_uart0_tx_int_clear(), 0)
#define RECV(pb)       gp5_uart0_recv_byte(pb)
#define SEND(b, n, t)  gp5_uart0_send(b, n, t)
#else
#include "type.h"
#include "uarts.h"
extern uint32_t gAPLLWorkFreq, gPLLWorkFreq;      /* the vendor library's record of the clocks we set */
#define CLOCKS()       do { gAPLLWorkFreq = 240000000u; gPLLWorkFreq = 288000000u; } while (0)
#define INIT()         UART0_Init(460800, 8, 0, 1)
#define RXFIFO_CLR()   UART0_IOCtl(UART_IOCTL_RXFIFO_CLR, 1)
#define TRIG0()        UART0_IOCtl(UART_IOCTL_RXFIFO_TRGRDEP_SET, 0)
#define RXINT(on)      UART0_IOCtl(UART_IOCTL_RXINT_SET, on)
#define RXSTAT()       UART0_IOCtl(UART_IOCTL_RXSTAT_GET, 0)
#define RXINT_CLR()    UART0_IOCtl(UART_IOCTL_RXINT_CLR, 1)
#define RXERR_CLR()    UART0_IOCtl(UART_IOCTL_RX_ERR_CLR, 1)
#define TXSTAT()       UART0_IOCtl(UART_IOCTL_TXSTAT_GET, 0)
#define TXINT_CLR()    UART0_IOCtl(UART_IOCTL_TXINT_CLR, 1)
#define RECV(pb)       UART0_RecvByte(pb)
#define SEND(b, n, t)  UART0_Send(b, n, t)
#endif

static void init(void)
{
    CLOCKS();
    REG(0x40021024u) |= 0x180u;                   /* UART clock = APLL, as the platform's clock set-up leaves it */
    REG(0x40021004u) &= 0xFFu;                    /* keep the divider small (and never divider + 1 == 0) */
    OUT(INIT());
}
static void rxfifo_clr(void) { OUT(RXFIFO_CLR()); }
static void trig0(void) { OUT(TRIG0()); }
static void rxint_on(void) { OUT(RXINT(1)); }
static void rxint_off(void) { OUT(RXINT(0)); }
static void rxstat(void) { OUT(RXSTAT()); }
static void rxint_clr(void) { OUT(RXINT_CLR()); }
static void rxerr_clr(void) { OUT(RXERR_CLR()); }
static void txstat(void) { OUT(TXSTAT()); }
static void txint_clr(void) { OUT(TXINT_CLR()); }
static void recv_some(void) { uint8_t b = 0; REG(0x40005024u) &= ~1u; OUT(RECV(&b) | (b << 8)); }
static void recv_none(void) { uint8_t b = 0; REG(0x40005024u) |= 1u; OUT(RECV(&b) | (b << 8)); }
static void send3(void)
{
    static uint8_t b[3] = { 0x11, 0x22, 0x33 };
    REG(0x40005014u) |= 1u << 9; REG(0x40005024u) |= 1u << 2;
    OUT(SEND(b, 3, 10));
}
const scenario_t scenarios[] = {
    { "init", init }, { "rxfifo_clr", rxfifo_clr }, { "trig0", trig0 }, { "rxint_on", rxint_on }, { "rxint_off", rxint_off },
    { "rxstat", rxstat }, { "rxint_clr", rxint_clr }, { "rxerr_clr", rxerr_clr }, { "txstat", txstat }, { "txint_clr", txint_clr },
    { "recv_some", recv_some }, { "recv_none", recv_none }, { "send3", send3 },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
