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
#ifdef GP5_USB_AUDIO
#include "gp5_firmware.h"
#include "usb_audio.h"
#endif

#define EP_CONTROL   0x00
#define EP_MIDI_IN   0x82      /* device -> host */
#define EP_MIDI_OUT  0x03      /* host -> device */

static const uint8_t device_desc[18] = {
    0x12, 0x01, 0x10, 0x01,  0x00, 0x00, 0x00, 0x40,
    0x09, 0x12,              /* idVendor  0x1209 (pid.codes) */
    0x01, 0x00,              /* idProduct 0x0001 (test PID) */
#ifdef GP5_USB_AUDIO
    0x01, 0x02,              /* bcdDevice 2.01: with USB audio (another set of interfaces: Windows looks again) */
#else
    0x00, 0x02,              /* bcdDevice 2.00 */
#endif
    0x01, 0x02, 0x03, 0x01
};

#ifndef GP5_USB_AUDIO
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
#else
/* With USB audio (usb_audio.c): one composite function, class compliant (UAC1 + USB-MIDI 1.0). Interface 0 audio
 * control, 1 playback, 2 recording, 3 the same MIDI streaming interface as above (endpoints 0x03 / 0x82). The
 * audio control header lists all three streaming interfaces (USB-MIDI asks for its interface to be in that list).
 * Terminals: USB streaming (1) -> speaker (2); line in (3) -> USB streaming (4). No feature unit (no volume or
 * mute: nothing for the host to set), one sample rate (the firmware's), so the host sends no class requests. */
#define SR0 (GP5_SAMPLE_RATE & 0xFF), ((GP5_SAMPLE_RATE >> 8) & 0xFF), ((GP5_SAMPLE_RATE >> 16) & 0xFF)
#define ISO_MPS (((GP5_SAMPLE_RATE / 1000u) + 1u) * 4u)
#define AC_LEN (11 + 12 + 9 + 12 + 9)
#define CFG_LEN (9 + 9 + AC_LEN + 2 * (9 + 9 + 7 + 11 + 9 + 7) + 74)
static const uint8_t config_desc[CFG_LEN] = {
    0x09,0x02,CFG_LEN & 0xFF,CFG_LEN >> 8,0x04,0x01,0x00,0x80,0x32,
    /* 0: audio control */
    0x09,0x04,0x00,0x00,0x00,0x01,0x01,0x00,0x00,
    0x0B,0x24,0x01,0x00,0x01,AC_LEN,0x00,0x03,0x01,0x02,0x03,
    0x0C,0x24,0x02,0x01,0x01,0x01,0x00,0x02,0x03,0x00,0x00,0x00,   /* IT 1: USB streaming, 2 ch (L, R) */
    0x09,0x24,0x03,0x02,0x01,0x03,0x00,0x01,0x00,                  /* OT 2: speaker, from 1 */
    0x0C,0x24,0x02,0x03,0x03,0x06,0x00,0x02,0x03,0x00,0x00,0x00,   /* IT 3: line connector, 2 ch */
    0x09,0x24,0x03,0x04,0x01,0x01,0x00,0x03,0x00,                  /* OT 4: USB streaming, from 3 */
    /* 1: playback (alternate 1 streams) */
    0x09,0x04,0x01,0x00,0x00,0x01,0x02,0x00,0x00,
    0x09,0x04,0x01,0x01,0x01,0x01,0x02,0x00,0x00,
    0x07,0x24,0x01,0x01,0x01,0x01,0x00,                            /* linked to terminal 1, PCM */
    0x0B,0x24,0x02,0x01,0x02,0x02,0x10,0x01,SR0,                   /* type I, 2 ch, 2 bytes, 16 bit, one rate */
    0x09,0x05,0x05,0x09,ISO_MPS & 0xFF,ISO_MPS >> 8,0x01,0x00,0x00,/* OUT 0x05, iso adaptive, every frame */
    0x07,0x25,0x01,0x00,0x00,0x00,0x00,
    /* 2: recording */
    0x09,0x04,0x02,0x00,0x00,0x01,0x02,0x00,0x00,
    0x09,0x04,0x02,0x01,0x01,0x01,0x02,0x00,0x00,
    0x07,0x24,0x01,0x04,0x01,0x01,0x00,                            /* linked to terminal 4 */
    0x0B,0x24,0x02,0x01,0x02,0x02,0x10,0x01,SR0,
    0x09,0x05,0x84,0x05,ISO_MPS & 0xFF,ISO_MPS >> 8,0x01,0x00,0x00,/* IN 0x84, iso asynchronous (the DSP's clock) */
    0x07,0x25,0x01,0x00,0x00,0x00,0x00,
    /* 3: MIDI streaming, as without audio */
    0x09,0x04,0x03,0x00,0x02,0x01,0x03,0x00,0x05,
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
#endif

static const uint8_t lang_id[4] = { 0x04, 0x03, 0x09, 0x04 };
/* manufacturer, product (the name the computer shows for the MIDI port and the audio device), serial, -, MIDI
 * interface. "OpenGP5" since 1.0 (was "GP-5 Open FW": the tools accept both). A new serial with a new name: Windows
 * keeps a device's names by its serial, so a new one makes it read them again (0008 before 1.0) */
/* The serial: one per sample rate with USB audio ("1044" / "1048"), "1000" without. Windows keeps a device's settings,
 * its audio format among them, by serial: one shared by a 44.1 kHz and a 48 kHz firmware kept the other's rate, and
 * the audio device couldn't be opened after a switch (FL Studio, 2026-11-26). "0009" in 1.0's test builds. */
#if defined(GP5_USB_AUDIO) && GP5_SAMPLE_RATE == 48000u
#define USB_SERIAL "1048"
#elif defined(GP5_USB_AUDIO)
#define USB_SERIAL "1044"
#else
#define USB_SERIAL "1000"
#endif
static const char *const strings[] = { 0, "OpenGP5", "OpenGP5", USB_SERIAL, 0, "OpenGP5 MIDI" };

static uint8_t setup[8];
#if defined(GP5_USB_AUDIO) && defined(GP5_USB_TRACE)
#include "hal/tick.h"
struct usb_setup_trace { uint32_t magic, pos, e[16][3]; };
volatile struct usb_setup_trace usb_setup_trace __attribute__((section(".noinit"), aligned(4)));
#endif
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
#ifdef GP5_USB_AUDIO
        if (configured) usb_audio_configured(); else usb_audio_reset();
#endif
        break;
    case 10:                                                             /* GET_INTERFACE */
#ifdef GP5_USB_AUDIO
        resp[0] = (uint8_t)usb_audio_get_interface(setup[4]);
#endif
        gp5_usb_control_send(resp, 1, 3); break;
    case 11:                                                             /* SET_INTERFACE */
#ifdef GP5_USB_AUDIO
        usb_audio_set_interface(setup[4], setup[2]);
#endif
        break;
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
#ifdef GP5_USB_AUDIO
        usb_audio_reset();
#endif
    }
    if (gp5_usb_setup(setup, 8, &n) != GP5_USB_OK) return;
    usb_setups++;
#if defined(GP5_USB_AUDIO) && defined(GP5_USB_TRACE)
    {   /* trace builds: the last 16 SETUP packets, in .noinit RAM: [bytes 0..3] [bytes 4..7] [ms] */
        uint32_t k;
        if (usb_setup_trace.magic != 0x53455455u) { usb_setup_trace.magic = 0x53455455u; usb_setup_trace.pos = 0; }
        k = usb_setup_trace.pos++ & 15u;
        usb_setup_trace.e[k][0] = setup[0] | setup[1] << 8 | setup[2] << 16 | (uint32_t)setup[3] << 24;
        usb_setup_trace.e[k][1] = setup[4] | setup[5] << 8 | setup[6] << 16 | (uint32_t)setup[7] << 24;
        usb_setup_trace.e[k][2] = gp5_ms();
    }
#endif

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
