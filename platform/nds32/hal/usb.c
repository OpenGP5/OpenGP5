/* GP-5 open platform: the BP1048's USB device controller. Control and bulk endpoints are polled from the main
 * loop; isochronous endpoints (USB audio) are served from the USB interrupt (line 20), one packet a millisecond.
 *
 * The controller sits at 0x40000300 with an indexed endpoint register file:
 *   +0x00 function address        +0x01 power / mode           +0x0A bus events (bit 2 = reset)
 *   +0x0E endpoint index          +0x10 TX max packet          +0x12 control/status: endpoint 0, or TX
 *   +0x14 RX max packet           +0x16 RX control/status      +0x18 received byte count
 *   +0x20 + 4 * endpoint: FIFO
 * Endpoint 0 control/status bits used: 0 packet received, 1 packet ready to send, 4 setup end,
 * 5 send stall, 6 received packet serviced, 7 setup end serviced, 8 flush.
 *
 * Same register accesses, widths, order and waits as the vendor library's otg_device_hcd / otg_hal /
 * otg_detect functions the platform called (checked by difference in the simulator,
 * tools/nds32_drvtest/usb_cases.c). Each wait gives up through a 1 ms-tick deadline with the vendor's
 * one-call-late expiry (gp5_timeout_late). */
#include <stdint.h>
#include "usb.h"
#include "tick.h"
#include <nds32_intrinsic.h>
#include "../start/irq.h"

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))

#define USB       0x40000300u
#define FADDR     R8(USB + 0x00)
#define POWER     R8(USB + 0x01)
#define EVENTS    R8(USB + 0x0A)
#define INDEX     R8(USB + 0x0E)
#define TXMAXP16  R16(USB + 0x10)
#define CSR8      R8(USB + 0x12)
#define CSR16     R16(USB + 0x12)
#define RXMAXP16  R16(USB + 0x14)
#define RXCSR8    R8(USB + 0x16)
#define RXCSR16   R16(USB + 0x16)
#define COUNT     R8(USB + 0x18)
#define COUNT16   R16(USB + 0x18)
#define INTRTX    R8(USB + 0x02)       /* endpoint interrupt flags (cleared by reading): TX = IN endpoints */
#define INTRRX    R8(USB + 0x04)       /*                                                RX = OUT endpoints */
#define INTRTXE   R8(USB + 0x06)       /* which endpoints may raise the interrupt line */
#define INTRRXE   R8(USB + 0x08)
#define INTRUSBE  R8(USB + 0x0B)       /* bus events that may raise it (none) */
#define FIFO_ADDR(ep) (USB + 0x20 + 4u * (ep))

#define PHY       0x40030004u          /* USB pad / PHY control */
#define RESET0    0x40022000u          /* block reset registers (bit set = running) */
#define RESET1    0x40022004u

volatile uint32_t gp5_usb_errors;      /* transfers that failed (the vendor printed a message instead) */

/* ---- FIFO: whole words first, then the remaining bytes ---- */
static void fifo_write(unsigned ep, const uint8_t *buf, unsigned len)
{
    unsigned words = len >> 2, rest = len & 3u, i;
    volatile uint32_t *f32 = (volatile uint32_t *)FIFO_ADDR(ep & 0xFu);
    volatile uint8_t *f8 = (volatile uint8_t *)FIFO_ADDR(ep & 0xFu);

    for (i = 0; i < words; i++, buf += 4)
        *f32 = (uint32_t)buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
    while (rest--) *f8 = *buf++;
}

static void fifo_read(unsigned ep, uint8_t *buf, unsigned len)
{
    unsigned words = len >> 2, rest = len & 3u, i;
    volatile uint32_t *f32 = (volatile uint32_t *)FIFO_ADDR(ep & 0xFu);
    volatile uint8_t *f8 = (volatile uint8_t *)FIFO_ADDR(ep & 0xFu);
    uint32_t w;

    for (i = 0; i < words; i++, buf += 4) {
        w = *f32;
        buf[0] = (uint8_t)w; buf[1] = (uint8_t)(w >> 8); buf[2] = (uint8_t)(w >> 16); buf[3] = (uint8_t)(w >> 24);
    }
    while (rest--) *buf++ = *f8;
}

/* ---- waits ---- */
enum { WAIT_CONTROL_IN, WAIT_BULK_IN, WAIT_BULK_OUT };

/* 1 when the endpoint (already selected) finished, 0 when the deadline passed. */
static int wait_done(int what, uint32_t ms)
{
    gp5_timeout_t t;

    gp5_timeout_set(&t, ms);
    for (;;) {
        if (what == WAIT_CONTROL_IN) { if (!(CSR8 & 0x2u)) return 1; }        /* packet taken */
        else if (what == WAIT_BULK_IN) { if (!(CSR8 & 0x3u)) return 1; }      /* sent, FIFO empty */
        else { if (RXCSR8 & 0x1u) return 1; }                                 /* packet arrived */
        if (gp5_timeout_late(&t)) return 0;
    }
}

/* ---- the pads: read-modify-write with interrupts held off ---- */
static void phy_update(uint32_t clear, uint32_t set)
{
    unsigned on = gp5_irq_global_state();
    gp5_irq_global(0);
    R32(PHY) = (R32(PHY) & ~clear) | set;
    if (on) gp5_irq_global(1);
}

void gp5_usb_address(unsigned addr)
{
    gp5_timeout_t t;

    gp5_timeout_set(&t, 5);
    while (!gp5_timeout_late(&t))                   /* let a status stage in progress finish first */
        if (CSR8 & 0x10u) break;
    FADDR = (uint8_t)(addr & 0x7Fu);
}

void gp5_usb_init(void)
{
    R32(RESET0) &= ~0x400u;                         /* reset the controller */
    gp5_delay_us(1);
    R32(RESET0) |= 0x400u;
    gp5_delay_us(1);
    R32(RESET1) &= ~0x100u;
    gp5_delay_us(1);
    R32(RESET1) |= 0x100u;
    phy_update(0x60u, 0);                           /* pull-downs off */
    phy_update(0x03u, 0);                           /* D+ pull-up off (looks unplugged) */
    phy_update(0, 0x80u);                           /* full speed */
    R32(USB + 0x328) = 9;
    R16(PHY) &= ~0x2000u;
    FADDR = 0;
    POWER = (uint8_t)((POWER | 0x40u) & ~0x20u);
    gp5_usb_address(0);
    phy_update(0, 0x03u);                           /* D+ pull-up on: the host sees the device */
}

unsigned gp5_usb_events(void) { return EVENTS; }

int gp5_usb_setup(uint8_t *buf, unsigned len, unsigned *got)
{
    unsigned n;

    INDEX = 0;
    if (!(CSR8 & 0x1u)) return GP5_USB_NONE;
    n = COUNT;
    if (!(len < n)) len = n;
    fifo_read(0, buf, len);
    CSR8 |= 0xC0u;                                  /* packet serviced, setup end serviced */
    *got = len;
    return GP5_USB_OK;
}

int gp5_usb_control_send(const uint8_t *buf, int len, uint32_t ms)
{
    unsigned n;

    INDEX = 0;
    do {                                            /* len 0 sends one empty packet */
        n = len < 0x41 ? (uint8_t)len : 64u;
        len -= (int)n;
        fifo_write(0, buf, n);
        CSR8 |= 0x2u;
        if (!wait_done(WAIT_CONTROL_IN, ms)) return GP5_USB_FAILED;
        buf += n;
    } while (len);
    return GP5_USB_OK;
}

/* Reads up to len bytes of an OUT data stage into buf. (As the vendor's: every packet lands at the start
 * of buf; the platform only reads to discard.) */
int gp5_usb_control_receive(uint8_t *buf, unsigned len, unsigned *got, uint32_t ms)
{
    gp5_timeout_t t;
    unsigned n;

    gp5_timeout_set(&t, ms);
    INDEX = 0;
    *got = 0;
    while (!gp5_timeout_late(&t)) {
        if (!(CSR8 & 0x1u)) continue;
        n = COUNT;
        if (len < n) n = len;
        fifo_read(0, buf, n);
        CSR8 |= 0x40u;
        *got += n;
        if (*got == len) return GP5_USB_OK;
    }
    return GP5_USB_FAILED;
}

void gp5_usb_stall(unsigned ep)
{
    INDEX = (uint8_t)(ep & 0xFu);
    if (ep == 0) { CSR16 |= 0x120u; return; }       /* endpoint 0: stall + flush */
    if (!(ep & 0x80u)) return;                      /* (the vendor stalls IN endpoints only) */
    CSR8 |= 0x08u;                                  /* flush */
    CSR16 |= 0x10u;                                 /* stall */
    gp5_delay_ms(5);
    CSR8 = (uint8_t)((CSR8 & ~0x10u) | 0x40u);      /* stall off, data toggle reset */
}

void gp5_usb_bulk_in_setup(unsigned ep)
{
    INDEX = (uint8_t)(ep & 0x7Fu);
    TXMAXP16 = 0x40;
    CSR16 = 0xA048;
}

void gp5_usb_bulk_out_setup(unsigned ep)
{
    INDEX = (uint8_t)(ep & 0x7Fu);
    CSR16 = 0;
    RXCSR16 = 0x90;
    RXMAXP16 = 0x40;
}

int gp5_usb_bulk_send(unsigned ep, const uint8_t *buf, int len, uint32_t ms)
{
    unsigned idx = ep & 0xFu, n;

    INDEX = (uint8_t)idx;
    do {
        n = len < 0x41 ? (unsigned)len : 64u;
        fifo_write(idx, buf, n);
        CSR16 = 0x2001;                             /* packet ready (TX mode) */
        len -= (int)n;
        buf += n;
        if (!wait_done(WAIT_BULK_IN, ms)) { gp5_usb_errors++; return GP5_USB_FAILED; }
    } while (len);
    return GP5_USB_OK;
}

int gp5_usb_bulk_receive(unsigned ep, uint8_t *buf, unsigned len, unsigned *got, uint32_t ms)
{
    unsigned idx = ep & 0xFu, n, total = 0;
    uint16_t left = (uint16_t)len;

    INDEX = (uint8_t)idx;
    if ((int)len < 0x40 && !(RXCSR8 & 0x1u)) { *got = 0; return GP5_USB_NONE; }
    for (;;) {
        if (!wait_done(WAIT_BULK_OUT, ms)) { gp5_usb_errors++; return GP5_USB_FAILED; }
        n = COUNT;
        if (left < n) { gp5_usb_errors++; *got = total; return GP5_USB_FAILED; }
        fifo_read(idx, buf, n);
        RXCSR8 &= ~0x1u;                            /* packet taken */
        buf += n;
        total += n;
        left = (uint16_t)(left - n);
        if (!left || n < 0x40) break;
    }
    *got = total;
    return total ? GP5_USB_OK : GP5_USB_NONE;
}

/* ---- isochronous endpoints, served from the USB interrupt ----
 * The controller is a Mentor-style one (indexed CSRs, clear-on-read interrupt flags). Settings as the vendor's
 * OTG_DeviceEndpointReset / OTG_DeviceISOSend / OTG_DeviceISOReceive / UsbInterrupt (libDriver.a, read from the
 * disassembly, work/usb-audio/re): IN TXCSR 0x6048 (TX mode, iso, clear toggle, flush), OUT RXCSR 0x4090 (iso,
 * clear toggle, flush). Endpoints 4 and 5 have the FIFOs for 192-byte packets (the vendor's DEVICE_FS_ISO_*). */
static gp5_usb_iso_fn iso_fn[8];
static uint8_t iso_mask;               /* endpoints whose interrupt is on (one bit per endpoint number, IN or OUT) */

static void irq_mask_apply(void)
{
    INTRTXE = iso_mask;
    INTRRXE = iso_mask;
    INTRUSBE = 0;
}

/* Setting an iso endpoint up flushes it, and a flush of a waiting packet raises the endpoint's interrupt flag
 * (latched even while the interrupt is off). That stale flag is read away here, so it can't fire later with a new
 * packet waiting (2026-11-23: it did, see gp5_usb_iso_send). Only the one direction's flags are read: reading the
 * other would also take away a playback packet's flag while playback runs. */
void gp5_usb_iso_in_setup(unsigned ep, unsigned maxp)
{
    unsigned on = gp5_irq_global_state();
    gp5_irq_global(0);
    {   uint8_t idx = INDEX;
        INDEX = (uint8_t)(ep & 0xFu);
        CSR16 = 0x6048;
        TXMAXP16 = (uint16_t)maxp;
        (void)INTRTX;                               /* the flush's flag (and endpoint 0's, which nothing uses) */
        INDEX = idx;
    }
    if (on) gp5_irq_global(1);
}

void gp5_usb_iso_out_setup(unsigned ep, unsigned maxp)
{
    unsigned on = gp5_irq_global_state();
    gp5_irq_global(0);
    {   uint8_t idx = INDEX;
        INDEX = (uint8_t)(ep & 0xFu);
        CSR16 = 0;
        RXCSR16 = 0x4090;
        RXMAXP16 = (uint16_t)maxp;
        (void)INTRRX;                               /* (bulk OUT is polled through its CSR: its flag isn't needed) */
        INDEX = idx;
    }
    if (on) gp5_irq_global(1);
}

/* Queue one IN packet (len bytes, 0 = an empty packet). From the endpoint's interrupt callback, or with
 * interrupts off. Returns 0, and queues nothing, while the last packet is still waiting for the host: flushing
 * it would raise the endpoint's interrupt again at once, and the callback would flush again, for ever (the storm
 * of 2026-11-23, which froze the pedal). Normally the interrupt comes when the packet is gone. */
int gp5_usb_iso_send(unsigned ep, const uint32_t *words, unsigned len)
{
    volatile uint32_t *f32 = (volatile uint32_t *)FIFO_ADDR(ep & 0xFu);
    uint8_t idx = INDEX;
    unsigned i, n = len >> 2;

    INDEX = (uint8_t)(ep & 0xFu);
    if (CSR16 & 0x3u) { INDEX = idx; return 0; }   /* a packet is still waiting (ready, or in the FIFO) */
    for (i = 0; i < n; i++) *f32 = words[i];
    CSR16 = 0x6041;                                 /* packet ready (as the vendor's OTG_DeviceISOSend) */
    INDEX = idx;
    return 1;
}

/* The OUT packet waiting at an iso endpoint, read into words[] (whole words, at most max bytes). Returns its
 * length in bytes (0 if none). From the endpoint's interrupt callback. */
unsigned gp5_usb_iso_receive(unsigned ep, uint32_t *words, unsigned max)
{
    volatile uint32_t *f32 = (volatile uint32_t *)FIFO_ADDR(ep & 0xFu);
    uint8_t idx = INDEX;
    unsigned len = 0, i;

    INDEX = (uint8_t)(ep & 0xFu);
    if (RXCSR8 & 0x1u) {
        len = COUNT16;
        if (len > max) len = max;
        for (i = 0; i < (len >> 2); i++) words[i] = *f32;
        RXCSR16 &= ~0x1u;                           /* packet taken */
        RXCSR8 = 0x80;                              /* (as the vendor: errors cleared) */
    }
    INDEX = idx;
    return len;
}

/* fn(ep) runs from the USB interrupt when endpoint ep (its number, IN or OUT) finished a packet; 0 = off. */
void gp5_usb_iso_irq(unsigned ep, gp5_usb_iso_fn fn)
{
    unsigned on = gp5_irq_global_state();
    ep &= 7u;
    gp5_irq_global(0);
    iso_fn[ep] = fn;
    if (fn) iso_mask |= (uint8_t)(1u << ep); else iso_mask &= (uint8_t)~(1u << ep);
    irq_mask_apply();
    if (on) gp5_irq_global(1);
    if (iso_mask) { gp5_irq_priority(GP5_IRQ_USB, 0); gp5_irq_enable(GP5_IRQ_USB); }
    else gp5_irq_disable(GP5_IRQ_USB);
}

volatile uint32_t gp5_usb_irq_cycles, gp5_usb_irq_count, gp5_usb_irq_worst;   /* its cost (performance counter 0) */
volatile uint32_t gp5_usb_irq_storms;  /* times the guard below switched the interrupt off */

#ifdef GP5_USB_TRACE
/* Trace builds (GP5_USB_TRACE=1): the last 16 interrupts, in .noinit RAM (kept across a restart), frozen at the first storm (below):
 * per entry [ms << 16 | INTRRX << 8 | INTRTX] [TXCSR of endpoint 4 | RXCSR of endpoint 5 << 16]
 * [iso_mask | INTRTXE << 8 | INTRRXE << 16 | INTRUSBE << 24] [INT_PEND2]. pos = entries written. */
struct gp5_usb_trace { uint32_t magic, pos, frozen, e[16][4]; };   /* (16: debug builds of big firmwares must fit) */
volatile struct gp5_usb_trace gp5_usb_trace __attribute__((section(".noinit"), aligned(4)));
static void trace(unsigned tx, unsigned rx)
{
    uint32_t k;
    if (gp5_usb_trace.magic != 0x55534255u) { gp5_usb_trace.magic = 0x55534255u; gp5_usb_trace.pos = 0; gp5_usb_trace.frozen = 0; }
    if (gp5_usb_trace.frozen) return;
    k = gp5_usb_trace.pos & 15u;
    gp5_usb_trace.e[k][0] = (gp5_ms() & 0xFFFFu) << 16 | (rx & 0xFFu) << 8 | (tx & 0xFFu);
    INDEX = 4; gp5_usb_trace.e[k][1] = CSR16;
    INDEX = 5; gp5_usb_trace.e[k][1] |= (uint32_t)RXCSR16 << 16;
    gp5_usb_trace.e[k][2] = iso_mask | (uint32_t)INTRTXE << 8 | (uint32_t)INTRRXE << 16 | (uint32_t)INTRUSBE << 24;
    gp5_usb_trace.e[k][3] = __nds32__mfsr(NDS32_SR_INT_PEND2);
    gp5_usb_trace.pos++;
}
#define trace_freeze() (gp5_usb_trace.frozen = 1)
#else
#define trace(tx, rx) ((void)0)
#define trace_freeze() ((void)0)
#endif
volatile uint32_t gp5_usb_irq_empty;   /* calls that found no flag of ours */

/* The USB interrupt (start/irq.c, line 20). Saves and restores the endpoint index: the main loop's polled
 * control and bulk transfers select endpoints through the same register.
 * Guard: more than 20 calls within one tick of the 1 ms clock (two are normal) switch the interrupt off for good
 * (counted in gp5_usb_irq_storms): a source that never clears must not take the processor over. The tick may
 * itself be held up while this runs, so the count is per tick seen, not per real millisecond. */
void gp5_usb_irq(void)
{
    static uint32_t last_ms, calls;
    uint32_t c0 = __nds32__mfsr(NDS32_SR_PFMC0), c, now = gp5_ms();
    uint8_t idx = INDEX;
    unsigned tx = INTRTX, rx = INTRRX, flags = (tx | rx) & iso_mask, ep;

    trace(tx, rx);
    if (now != last_ms) { last_ms = now; calls = 0; }
    if (++calls > 20u) {
        iso_mask = 0;
        irq_mask_apply();
        gp5_irq_disable(GP5_IRQ_USB);
        gp5_usb_irq_storms++;
        trace_freeze();
        INDEX = idx;
        return;
    }
 if (!flags) gp5_usb_irq_empty++;
    for (ep = 0; flags; ep++, flags >>= 1)
        if ((flags & 1u) && iso_fn[ep]) iso_fn[ep](ep);
    INDEX = idx;
    c = __nds32__mfsr(NDS32_SR_PFMC0) - c0;
    gp5_usb_irq_cycles += c;
    gp5_usb_irq_count++;
    if (c > gp5_usb_irq_worst) gp5_usb_irq_worst = c;
}
