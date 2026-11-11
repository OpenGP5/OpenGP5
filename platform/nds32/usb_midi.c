/* USB-MIDI device for the GP-5 standalone app, on our own polled device-controller driver (hal/usb.c).
 *
 * Poll the bus event and SETUP packet from the main loop, answer the standard requests, no class requests.
 * Configuration descriptor: the factory loader's own USB-MIDI descriptor, byte for byte (it enumerates
 * on Linux, macOS and Windows as the recovery device): one MIDIStreaming interface, bulk OUT 0x03,
 * bulk IN 0x82, 64-byte packets. Those endpoint numbers are also the ones the stock app uses for MIDI.
 *
 * IDs: 0x1209:0x0001 is the pid.codes test PID (for private testing only, not for distribution).
 *
 * Needs: the USB clock (hal/clock.c: APLL 240 MHz / 4) and the 1 ms tick (hal/tick.c): every transfer
 * waits with a deadline, so a transfer the host never completes cannot hang the main loop for good. */
#include <stdint.h>
#include "hal/usb.h"
#include "usb_midi.h"

#define EP_CONTROL   0x00
#define EP_MIDI_IN   0x82      /* device -> host */
#define EP_MIDI_OUT  0x03      /* host -> device */

static const uint8_t device_desc[18] = {
    0x12, 0x01, 0x10, 0x01,  0x00, 0x00, 0x00, 0x40,
    0x09, 0x12,              /* idVendor  0x1209 (pid.codes) */
    0x01, 0x00,              /* idProduct 0x0001 (test PID) */
    0x00, 0x02,              /* bcdDevice 2.00 */
    0x01, 0x02, 0x03, 0x01
};

static const uint8_t config_desc[83] = {
    0x09,0x02,0x53,0x00,0x01,0x01,0x00,0x80,0x32,
    0x09,0x04,0x00,0x00,0x02,0x01,0x03,0x00,0x05,
    0x07,0x24,0x01,0x00,0x01,0x41,0x00,
    0x06,0x24,0x02,0x01,0x01,0x00,
    0x06,0x24,0x02,0x02,0x02,0x00,
    0x09,0x24,0x03,0x01,0x03,0x01,0x02,0x01,0x00,
    0x09,0x24,0x03,0x02,0x04,0x01,0x01,0x01,0x00,
    0x09,0x05,0x03,0x02,0x40,0x00,0x00,0x00,0x00,
    0x05,0x25,0x01,0x01,0x01,
    0x09,0x05,0x82,0x02,0x40,0x00,0x00,0x00,0x00,
    0x05,0x25,0x01,0x01,0x03
};

static const uint8_t lang_id[4] = { 0x04, 0x03, 0x09, 0x04 };
static const char *const strings[] = { 0, "GP-5 Open", "GP-5 Open FW", "0008", 0, "GP-5 Open MIDI" };

static uint8_t setup[8];
static uint8_t ctl_buf[64];
static uint8_t configured;

volatile uint32_t usb_setups, usb_resets;   /* counters for the on-screen status / debugger (failed transfers: gp5_usb_errors) */

/* The controller's indexed endpoint registers (hal/usb.c: index at +0x0E, RX control/status at +0x16,
 * bit 0 = a received packet is waiting). */
#define USB_INDEX   (*(volatile uint8_t *)0x4000030E)
#define USB_RXCSR_L (*(volatile uint8_t *)0x40000316)

int usb_midi_configured(void) { return configured; }

static void get_descriptor(void)
{
    const uint8_t *p = 0;
    unsigned len = 0, want = setup[6] | (setup[7] << 8);

    switch (setup[3]) {
    case 1: p = device_desc; len = sizeof device_desc; break;
    case 2: p = config_desc; len = sizeof config_desc; break;
    case 3:
        if (setup[2] == 0) { p = lang_id; len = sizeof lang_id; }
        else if (setup[2] < sizeof strings / sizeof strings[0] && strings[setup[2]]) {
            const char *s = strings[setup[2]];
            unsigned n = 0;
            while (s[n] && n < 30) { ctl_buf[2 + 2 * n] = (uint8_t)s[n]; ctl_buf[3 + 2 * n] = 0; n++; }
            ctl_buf[0] = (uint8_t)(2 + 2 * n); ctl_buf[1] = 3;
            p = ctl_buf; len = ctl_buf[0];
        }
        break;
    }
    if (!p) { gp5_usb_stall(EP_CONTROL); return; }
    if (len > want) len = want;
    gp5_usb_control_send(p, (int)len, 3);
}

static void standard_request(void)
{
    uint8_t resp[2] = { 0, 0 };

    switch (setup[1]) {
    case 0:  gp5_usb_control_send(resp, 2, 10); break;                  /* GET_STATUS */
    case 1:  break;                                                      /* CLEAR_FEATURE */
    case 3:  gp5_usb_stall(setup[4]); break;                       /* SET_FEATURE */
    case 5:  gp5_usb_address(setup[2] & 0x7F); break;               /* SET_ADDRESS */
    case 6:  get_descriptor(); break;                                    /* GET_DESCRIPTOR */
    case 8:  resp[0] = configured; gp5_usb_control_send(resp, 1, 3); break;   /* GET_CONFIGURATION */
    case 9:                                                              /* SET_CONFIGURATION */
        gp5_usb_bulk_in_setup(EP_MIDI_IN);
        gp5_usb_bulk_out_setup(EP_MIDI_OUT);
        configured = setup[2] ? 1 : 0;
        break;
    case 10: gp5_usb_control_send(resp, 1, 3); break;                   /* GET_INTERFACE */
    case 11: break;                                                      /* SET_INTERFACE */
    default: gp5_usb_stall(EP_CONTROL); break;
    }
}

void usb_midi_init(void)
{
    configured = 0;
    gp5_usb_init();
}

/* Control endpoint: call often from the main loop. */
void usb_midi_poll(void)
{
    unsigned n, want;

    if (gp5_usb_events() & GP5_USB_EVENT_RESET) {
        gp5_usb_address(0);
        configured = 0;
        usb_resets++;
    }
    if (gp5_usb_setup(setup, 8, &n) != GP5_USB_OK) return;
    usb_setups++;

    want = setup[6] | (setup[7] << 8);
    if (!(setup[0] & 0x80) && want) {                /* OUT data stage: read and discard */
        while (want) {
            unsigned chunk = want > 64 ? 64 : want;
            gp5_usb_control_receive(ctl_buf, chunk, &n, 10);
            want -= chunk;
        }
    }
    if (((setup[0] >> 5) & 3) == 0) standard_request();
    else gp5_usb_stall(EP_CONTROL);
}

/* One bulk OUT packet of USB-MIDI events (4 bytes each) into buf[64]. Returns the byte count, 0 if
 * none is waiting; never blocks. (gp5_usb_bulk_receive with a 64-byte buffer waits at least one tick
 * for a packet, so look at the packet-ready bit first.) */
unsigned usb_midi_read(uint8_t *buf)
{
    unsigned n = 0;
    if (!configured) return 0;
    USB_INDEX = EP_MIDI_OUT & 0x0F;
    if (!(USB_RXCSR_L & 1)) return 0;
    if (gp5_usb_bulk_receive(EP_MIDI_OUT, buf, 64, &n, 2) != GP5_USB_OK) return 0;
    return n;
}

/* Send USB-MIDI events (a multiple of 4 bytes, at most 64). Returns 1 if the host took them. */
int usb_midi_write(uint8_t *buf, unsigned len)
{
    if (!configured) return 0;
    return gp5_usb_bulk_send(EP_MIDI_IN, buf, (int)len, 20) == GP5_USB_OK;
}
