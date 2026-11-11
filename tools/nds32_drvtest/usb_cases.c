/* USB device controller scenarios: every call the platform makes (usb_midi.c), with its real arguments.
 * Builds against the vendor library or (-DOURS) against platform/nds32/hal/usb.c.
 *
 * The simulator has no USB host and no running tick, so both sides get the same stand-ins here:
 * a deadline expires after a fixed number of checks (counted, so the number of polls is compared too),
 * delays only count, and the interrupt on/off calls only count. Status bits a call waits for come from
 * the random start state or are set by the scenario. Return values, byte counts and a checksum of what
 * was read land in a scratch area of the peripheral space so the log compares them as well. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = {
    { 0x40000300u, 0x40000360u },   /* controller + FIFOs */
    { 0x40000620u, 0x40000630u },
    { 0x40022000u, 0x40022008u },   /* block resets */
    { 0x40030000u, 0x40030008u },   /* USB pads */
};
const unsigned n_windows = 4;
#define REG(a) (*(volatile uint32_t *)(a))
#define OUT(i, v) (REG(0x400FFF00u + 4u * (i)) = (uint32_t)(v))

/* ---- shared stand-ins ---- */
static uint32_t polls, delays_us, delays_ms, gie_calls, prints;
typedef struct { uint32_t left, start; uint8_t expired; } dl_t;
static void dl_set(void *t, uint32_t ms) { ((dl_t *)t)->left = 3 + ms % 4; ((dl_t *)t)->expired = 0; }
static int dl_late(void *t)
{
    dl_t *d = (dl_t *)t;
    int was = d->expired;
    polls++;
    if (!was && --d->left == 0) d->expired = 1;
    return was;
}
#ifdef OURS
void gp5_delay_us(uint32_t us) { delays_us += us; }
void gp5_delay_ms(uint32_t ms) { delays_ms += ms; }
#else
void __udelay(uint32_t us) { delays_us += us; }
void DelayMs(uint32_t ms) { delays_ms += ms; }
#endif

#ifdef OURS
#include "hal/usb.h"
#include "hal/tick.h"
#include "start/irq.h"
void gp5_timeout_set(gp5_timeout_t *t, uint32_t ms) { dl_set(t, ms); }
int gp5_timeout_late(gp5_timeout_t *t) { return dl_late(t); }
unsigned gp5_irq_global_state(void) { gie_calls++; return 1; }
void gp5_irq_global(int on) { gie_calls += on ? 16 : 1; }
#define ERRS()                 (prints + gp5_usb_errors)
#define INIT()                 gp5_usb_init()
#define EVENTS()               gp5_usb_events()
#define ADDRESS(a)             gp5_usb_address(a)
#define SETUP(b, l, n)         (gp5_usb_setup(b, l, n) == GP5_USB_OK)
#define CSEND(b, l, t)         (gp5_usb_control_send(b, l, t) == GP5_USB_OK)
#define CRECV(b, l, n, t)      (gp5_usb_control_receive(b, l, n, t) == GP5_USB_OK)
#define STALL(e)               gp5_usb_stall(e)
#define BULK_IN_SETUP(e)       gp5_usb_bulk_in_setup(e)
#define BULK_OUT_SETUP(e)      gp5_usb_bulk_out_setup(e)
#define BSEND(e, b, l, t)      (gp5_usb_bulk_send(e, b, l, t) == GP5_USB_OK)
#define BRECV(e, b, l, n, t)   (gp5_usb_bulk_receive(e, b, l, n, t) == GP5_USB_OK)
typedef unsigned cnt_t;
#else
#include "type.h"
#include "otg_device_hcd.h"
void TimeOutSet(void *t, uint32_t ms) { dl_set(t, ms); }
int IsTimeOut(void *t) { return dl_late(t); }
int GIE_STATE_GET(void) { gie_calls++; return 1; }
void GIE_DISABLE(void) { gie_calls += 1; }
void GIE_ENABLE(void) { gie_calls += 16; }
int puts(const char *s) { (void)s; prints++; return 0; }
#define ERRS()                 prints
#define INIT()                 OTG_DeviceInit()
#define EVENTS()               OTG_DeviceBusEventGet()
#define ADDRESS(a)             OTG_DeviceAddressSet(a)
#define SETUP(b, l, n)         (OTG_DeviceSetupReceive(b, l, n) == DEVICE_NONE_ERR)
#define CSEND(b, l, t)         (OTG_DeviceControlSend(b, l, t) == DEVICE_NONE_ERR)
#define CRECV(b, l, n, t)      (OTG_DeviceControlReceive(b, l, n, t) == DEVICE_NONE_ERR)
#define STALL(e)               OTG_DeviceStallSend(e)
#define BULK_IN_SETUP(e)       OTG_DeviceEndpointReset(e, TYPE_BULK_IN)
#define BULK_OUT_SETUP(e)      OTG_DeviceEndpointReset(e, TYPE_BULK_OUT)
#define BSEND(e, b, l, t)      (OTG_DeviceBulkSend(e, b, l, t) == DEVICE_NONE_ERR)
#define BRECV(e, b, l, n, t)   (OTG_DeviceBulkReceive(e, b, l, n, t) == DEVICE_NONE_ERR)
typedef uint32_t cnt_t;
#endif

static void report(void)
{
    OUT(56, polls); OUT(57, delays_us); OUT(58, delays_ms); OUT(59, gie_calls); OUT(60, ERRS());
}

static uint8_t buf[200];
static void fill(void) { unsigned i; for (i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 37 + 11); }
static void sum_out(unsigned i, unsigned len)
{
    uint32_t s = 0; unsigned k;
    for (k = 0; k < len; k++) s = s * 31 + buf[k];
    OUT(i, s);
}
#define CTL       REG(0x40000310u)    /* bytes 0x312 / 0x313: endpoint 0 or TX control/status */
#define RXCTL     REG(0x40000314u)    /* bytes 0x316 / 0x317 */

static void init(void) { INIT(); report(); }
static void events(void) { OUT(0, EVENTS()); }
static void address0(void) { ADDRESS(0); report(); }
static void address_set(void) { ADDRESS(0x2A | 0x80); report(); }
static void address_setup_end(void) { CTL |= 0x100000u; ADDRESS(5); report(); }
static void setup_none(void) { cnt_t n = 99; CTL &= ~0x10000u; OUT(0, SETUP(buf, 8, &n)); OUT(1, n); report(); }
static void setup_8(void) { cnt_t n = 0; CTL |= 0x10000u; OUT(0, SETUP(buf, 8, &n)); OUT(1, n); sum_out(2, 8); report(); }
static void csend(unsigned len)
{
    fill(); CTL &= ~0x20000u;            /* never cleared by the stand-in host: ends in a timeout */
    OUT(0, CSEND(buf + 1, (int)len, 3)); report(); OUT(3, ERRS());
}
static void csend_0(void) { csend(0); }
static void csend_2(void) { csend(2); }
static void csend_18(void) { csend(18); }
static void csend_83(void) { csend(83); }
static void crecv(void) { cnt_t n = 0; CTL |= 0x10000u; OUT(0, CRECV(buf, 7, &n, 10)); OUT(1, n); sum_out(2, 8); report(); }
static void crecv_none(void) { cnt_t n = 0; CTL &= ~0x10000u; OUT(0, CRECV(buf, 64, &n, 10)); OUT(1, n); report(); }
static void stall0(void) { STALL(0x00); report(); }
static void stall_in(void) { STALL(0x82); report(); }
static void stall_out(void) { STALL(0x03); report(); }
static void bulk_setup(void) { BULK_IN_SETUP(0x82); BULK_OUT_SETUP(0x03); report(); }
static void bsend(unsigned len, int busy)
{
    fill();
    if (busy) CTL |= 0x10000u; else CTL &= ~0x30000u;
    OUT(0, BSEND(0x82, buf + 3, (int)len, 20)); report(); OUT(3, ERRS());
}
static void bsend_4(void) { bsend(4, 0); }
static void bsend_64(void) { bsend(64, 0); }
static void bsend_100(void) { bsend(100, 0); }
static void bsend_busy(void) { bsend(8, 1); }
static void brecv(unsigned len, int ready)
{
    cnt_t n = 0;
    if (ready) RXCTL |= 0x10000u; else RXCTL &= ~0x10000u;
    OUT(0, BRECV(0x03, buf, len, &n, 2)); OUT(1, n); sum_out(2, 64); report(); OUT(3, ERRS());
}
static void brecv_64(void) { brecv(64, 1); }
static void brecv_64_idle(void) { brecv(64, 0); }
static void brecv_8(void) { brecv(8, 1); }
static void brecv_8_idle(void) { brecv(8, 0); }

const scenario_t scenarios[] = {
    { "init", init }, { "events", events }, { "address0", address0 }, { "address_set", address_set },
    { "address_setup_end", address_setup_end }, { "setup_none", setup_none }, { "setup_8", setup_8 },
    { "csend_0", csend_0 }, { "csend_2", csend_2 }, { "csend_18", csend_18 }, { "csend_83", csend_83 },
    { "crecv", crecv }, { "crecv_none", crecv_none },
    { "stall0", stall0 }, { "stall_in", stall_in }, { "stall_out", stall_out }, { "bulk_setup", bulk_setup },
    { "bsend_4", bsend_4 }, { "bsend_64", bsend_64 }, { "bsend_100", bsend_100 }, { "bsend_busy", bsend_busy },
    { "brecv_64", brecv_64 }, { "brecv_64_idle", brecv_64_idle }, { "brecv_8", brecv_8 },
    { "brecv_8_idle", brecv_8_idle },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
