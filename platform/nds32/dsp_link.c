/* NDS32 side of the NDS32 <-> DSP control link (docs/platform/protocols.md, section 5).
 *
 * Hardware, as the stock app sets it up (stock 0x48768, called from the link engine at 0x14994):
 *   GPIO_PortAModeSet(GPIOA0, 2)     A0 = uart0_rxd_0
 *   GPIO_PortAModeSet(GPIOA1, 0xc)   A1 = uart0_txd_0
 *   UART0_Init(460800, 8, 0, 1)      (stock's config struct: baud 0x70800, 8 data bits, no parity, 1 stop)
 * with the UART clock on the APLL (stock clock setup: Clock_UARTClkSelect(APLL_CLK_MODE), done in main()).
 * Stock then runs the UART over DMA with a command queue. Here (FW 6) reception is interrupt driven:
 * UART0_Interrupt (line 7; start/irq.c's handler table calls it as a plain C function) moves every
 * received byte into a ring buffer, and dsp_link_poll(), called from the main loop, cuts the byte stream
 * into frames. So frames the DSP sends on its own (the 0x4001 report) and long replies are received
 * whenever they come. Sending is still the library's polled UART0_Send.
 * The receive FIFO is 4 bytes deep and a byte takes 22 us at 460800 baud, so an interrupt held off for
 * more than about 80 us loses bytes; the UART's overrun/framing flags are counted in dsp_rx_errs and a
 * frame with a bad CRC in dsp_rx_bad, so the hardware run shows whether that happens.
 *
 * UART0 register use, read from libDriver's UART0_IOCtl / UART0_RecvByte (base 0x40005000):
 *   RXSTAT_GET = [+0x14] & 0x3f: bit 0 = receive interrupt, bits 2..5 = error flags (the argument is ignored)
 *   RXINT_SET  = [+0x0c] bit 1,  RXINT_CLR = [+0x0c] |= 4,  RX_ERR_CLR = [+0x0c] |= 0x10
 *   UART0_RecvByte: returns 0 when [+0x24] bit 0 (FIFO empty) is set, else the byte from [+0x08]
 * Handler and set-up follow the SDK's own interrupt receiver (middleware/rtos/freertos/src/shell.c).
 *
 * Frame, both directions:  [crc8][len u16 LE = whole frame][0x01][payload]
 *   crc8 = poly 0x07, init 0, over the payload;  payload = [cmd u16 LE][args]
 * Requests carry [cmd][len u16 = 8][u32 argument] (hardware sniff: ab 0c 00 01 00 60 08 00 00 00 00 00).
 * Status-only frame (4 bytes): 01 04 00 <code>, code 0x14 = ACK, 0x13 = NAK. */
#include <stdint.h>
#include <stdbool.h>
#include "hal/gpio.h"
#include "hal/tick.h"
#include "start/irq.h"
#include "hal/uart0.h"
#include "dsp_link.h"

volatile uint32_t dsp_tx_frames, dsp_rx_bytes, dsp_rx_frames;
volatile uint32_t dsp_rx_errs, dsp_rx_lost, dsp_irq_off;
uint32_t dsp_rx_bad, dsp_acks, dsp_naks, dsp_unsol_frames;
uint16_t dsp_unsol_cmd;
uint8_t dsp_unsol[DSP_UNSOL_MAX];
unsigned dsp_unsol_len;

/* Ring buffer: the interrupt handler writes at ring_w, the main loop reads at ring_r. */
#define RING 512u                       /* power of two */
static volatile uint8_t ring[RING];
static volatile uint16_t ring_w;
static uint16_t ring_r;
static uint16_t idle_irqs;              /* handler entries in a row that found nothing to do */

/* The frame being assembled, and the reply dsp_link_query() is waiting for. */
static uint8_t acc[DSP_FRAME_MAX];
static unsigned acc_n;
static uint32_t acc_tick;
static uint16_t want_cmd;
static uint8_t *want_buf;
static unsigned want_max, want_len;
static int want_active;

static uint8_t crc8(const uint8_t *d, unsigned n)
{
    uint8_t c = 0;
    unsigned i;
    while (n--) {
        c ^= *d++;
        for (i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

/* out must hold 6 + nargs bytes. Returns the frame length. */
unsigned dsp_frame_build(uint8_t *out, uint16_t cmd, const uint8_t *args, unsigned nargs)
{
    unsigned i, len = 6 + nargs;
    out[1] = (uint8_t)len; out[2] = (uint8_t)(len >> 8); out[3] = 0x01;
    out[4] = (uint8_t)cmd; out[5] = (uint8_t)(cmd >> 8);
    for (i = 0; i < nargs; i++) out[6 + i] = args[i];
    out[0] = crc8(out + 4, len - 4);
    return len;
}

/* Look for a valid frame carrying `cmd` anywhere in buf (the window can start in the middle of another
 * frame). Copies its payload; returns the payload length, 0 if there is none. */
unsigned dsp_frame_find(const uint8_t *buf, unsigned n, uint16_t cmd, uint8_t *payload, unsigned max)
{
    unsigned off, len, i;
    for (off = 0; off + 6 <= n; off++) {
        len = buf[off + 1] | (buf[off + 2] << 8);
        if (buf[off + 3] != 0x01 || len < 6 || off + len > n) continue;
        if (crc8(buf + off + 4, len - 4) != buf[off]) continue;
        if ((buf[off + 4] | (buf[off + 5] << 8)) != cmd) { off += len - 1; continue; }
        len -= 4;
        if (len > max) len = max;
        for (i = 0; i < len; i++) payload[i] = buf[off + 4 + i];
        return len;
    }
    return 0;
}

static void drain(void)
{
    uint8_t b;
    unsigned n = 0;
    while (n < 64 && gp5_uart0_recv_byte(&b)) {
        uint16_t w = ring_w, next = (uint16_t)((w + 1) & (RING - 1));
        if (next != ring_r) { ring[w] = b; ring_w = next; } else dsp_rx_lost++;
        n++;
    }
    dsp_rx_bytes += n;
}

void UART0_Interrupt(void)
{
    uint32_t st = (uint32_t)gp5_uart0_rx_status();
    int did = 0;

    if (st & 0x01) {
        drain();
        gp5_uart0_rx_int_clear();
        did = 1;
    }
    if (st & 0x3C) {
        dsp_rx_errs++;
        gp5_uart0_rx_err_clear();
        gp5_uart0_rx_int_clear();
    }
    if (gp5_uart0_tx_int_pending()) gp5_uart0_tx_int_clear();

    /* Safety net: if the interrupt keeps firing with nothing to receive (a flag this handler does not
     * clear), switch the receive interrupt off so the main loop keeps running; dsp_link_poll() then
     * reads the receiver directly. */
    if (did) idle_irqs = 0;
    else if (++idle_irqs >= 2000) {
        gp5_uart0_rx_int_enable(0);
        NVIC_DisableIRQ(GP5_IRQ_UART0);
        dsp_irq_off = 1;
    }
}

void dsp_link_init(void)
{
    gp5_gpio_uart0_pins();                  /* A0 = UART0 receive, A1 = UART0 transmit */
    gp5_uart0_init(460800, 240000000u);    /* 8N1; the UART clock is the 240 MHz APLL (main.c) */

    gp5_uart0_rx_fifo_clear();
    gp5_uart0_rx_int_from_first_byte();      /* interrupt from the first byte */
    gp5_uart0_rx_int_enable(1);
    NVIC_EnableIRQ(GP5_IRQ_UART0);
    NVIC_SetPriority(GP5_IRQ_UART0, 2);
}

static void acc_drop(unsigned n)
{
    unsigned i;
    acc_n -= n;
    for (i = 0; i < acc_n; i++) acc[i] = acc[i + n];
}

static void frame_in(const uint8_t *pl, unsigned n)
{
    uint16_t cmd = (uint16_t)(pl[0] | (pl[1] << 8));
    unsigned i;

    dsp_rx_frames++;
    if (want_active && !want_len && cmd == want_cmd) {
        want_len = n > want_max ? want_max : n;
        for (i = 0; i < want_len; i++) want_buf[i] = pl[i];
        return;
    }
    dsp_unsol_frames++;
    dsp_unsol_cmd = cmd;
    dsp_unsol_len = n > DSP_UNSOL_MAX ? DSP_UNSOL_MAX : n;
    for (i = 0; i < dsp_unsol_len; i++) dsp_unsol[i] = pl[i];
}

/* Cut frames off the front of acc[]. A byte that cannot start a frame is dropped (resync). */
static void parse(void)
{
    unsigned len;
    for (;;) {
        if (acc_n < 4) return;
        len = acc[1] | (acc[2] << 8);
        if (len == 4 && acc[0] == 0x01 && (acc[3] == 0x14 || acc[3] == 0x13)) {
            if (acc[3] == 0x14) dsp_acks++; else dsp_naks++;
            acc_drop(4);
            continue;
        }
        if (acc[3] != 0x01 || len < 6 || len > sizeof acc) { acc_drop(1); continue; }
        if (acc_n < len) return;
        if (crc8(acc + 4, len - 4) != acc[0]) { dsp_rx_bad++; acc_drop(1); continue; }
        frame_in(acc + 4, len - 4);
        acc_drop(len);
    }
}

/* Main loop: take what the interrupt handler received and handle complete frames. Never waits. */
void dsp_link_poll(void)
{
    uint32_t now = gp5_ms();
    uint16_t w;
    int got = 0;

    if (dsp_irq_off) drain();           /* only after the safety net in UART0_Interrupt tripped */
    w = ring_w;
    while (ring_r != w) {
        if (acc_n == sizeof acc) acc_drop(1);
        acc[acc_n++] = ring[ring_r];
        ring_r = (uint16_t)((ring_r + 1) & (RING - 1));
        got = 1;
        if (acc_n >= 4) parse();
    }
    if (got) acc_tick = now;
    else if (acc_n && (uint32_t)(now - acc_tick) > 50) {   /* a frame that never completed */
        dsp_rx_bad++;
        acc_drop(1);
        parse();
        acc_tick = now;
    }
}

int dsp_link_send(uint16_t cmd, const uint8_t *args, unsigned nargs)
{
    uint8_t tx[6 + DSP_ARGS_MAX];
    if (nargs > DSP_ARGS_MAX) return 0;
    gp5_uart0_send(tx, dsp_frame_build(tx, cmd, args, nargs), 10);
    dsp_tx_frames++;
    return 1;
}

unsigned dsp_link_query(uint16_t cmd, const uint8_t *args, unsigned nargs,
                        uint8_t *reply, unsigned max, unsigned wait_ms)
{
    uint32_t t0;

    dsp_link_poll();                    /* frames that were already waiting are not this reply */
    want_cmd = cmd; want_buf = reply; want_max = max; want_len = 0; want_active = 1;
    if (!dsp_link_send(cmd, args, nargs)) { want_active = 0; return 0; }

    t0 = gp5_ms();
    do dsp_link_poll(); while (!want_len && (uint32_t)(gp5_ms() - t0) < wait_ms);
    want_active = 0;
    return want_len;
}

unsigned dsp_link_raw(const uint8_t *payload, unsigned n, uint8_t *reply, unsigned max, unsigned wait_ms,
                      uint8_t *status)
{
    static uint8_t tx[4 + DSP_RAW_MAX];
    uint32_t t0, acks0, naks0;
    unsigned i, len = n + 4;

    *status = 0;
    if (n < 2 || n > DSP_RAW_MAX) return 0;
    tx[0] = crc8(payload, n); tx[1] = (uint8_t)len; tx[2] = (uint8_t)(len >> 8); tx[3] = 0x01;
    for (i = 0; i < n; i++) tx[4 + i] = payload[i];

    dsp_link_poll();                    /* frames that were already waiting are not this answer */
    acks0 = dsp_acks; naks0 = dsp_naks;
    want_cmd = (uint16_t)(payload[0] | (payload[1] << 8));
    want_buf = reply; want_max = max; want_len = 0; want_active = 1;
    gp5_uart0_send(tx, len, 100);           /* 404 bytes take 9 ms at 460800 baud */
    dsp_tx_frames++;

    t0 = gp5_ms();
    do dsp_link_poll();
    while (!want_len && dsp_acks == acks0 && dsp_naks == naks0 && (uint32_t)(gp5_ms() - t0) < wait_ms);
    want_active = 0;
    if (want_len) *status = 1;
    else if (dsp_naks != naks0) *status = 0x13;
    else if (dsp_acks != acks0) *status = 0x14;
    return want_len;
}

uint32_t dsp_link_version(void)
{
    static const uint8_t args[6] = { 8, 0, 0, 0, 0, 0 };     /* [len u16 = 8][u32 0] */
    uint8_t r[12];

    /* reply payload: 00 60 | 0c 00 | 01 00 00 00 | version u32 LE */
    if (dsp_link_query(DSP_CMD_VERSION, args, sizeof args, r, sizeof r, 30) != 12) return 0;
    return r[8] | (r[9] << 8) | ((uint32_t)r[10] << 16) | ((uint32_t)r[11] << 24);
}
