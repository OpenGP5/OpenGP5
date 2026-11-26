#ifndef GP5_HAL_USB_H
#define GP5_HAL_USB_H
#include <stdint.h>
/* USB device controller: control and bulk polled, isochronous from the USB interrupt. Our own code (usb.c). The USB clock must be running (hal/clock.c) and
 * the 1 ms tick too: every wait gives up through it. */

enum { GP5_USB_OK = 0, GP5_USB_FAILED = 1, GP5_USB_NONE = 2 };

#define GP5_USB_EVENT_RESET 0x04u

void gp5_usb_init(void);                    /* reset the controller, then connect (D+ pull-up on) */
unsigned gp5_usb_events(void);              /* bus events since the last call (bit 2 = bus reset) */
void gp5_usb_address(unsigned addr);

/* Endpoint 0 */
int gp5_usb_setup(uint8_t *buf, unsigned len, unsigned *got);     /* GP5_USB_NONE if no SETUP waiting */
int gp5_usb_control_send(const uint8_t *buf, int len, uint32_t ms);
int gp5_usb_control_receive(uint8_t *buf, unsigned len, unsigned *got, uint32_t ms);
void gp5_usb_stall(unsigned ep);            /* endpoint 0, or an IN endpoint (0x8n) */

/* Bulk endpoints, 64-byte packets */
void gp5_usb_bulk_in_setup(unsigned ep);
void gp5_usb_bulk_out_setup(unsigned ep);
int gp5_usb_bulk_send(unsigned ep, const uint8_t *buf, int len, uint32_t ms);
int gp5_usb_bulk_receive(unsigned ep, uint8_t *buf, unsigned len, unsigned *got, uint32_t ms);

extern volatile uint32_t gp5_usb_errors;    /* failed bulk transfers */

/* Isochronous endpoints (USB audio), served from the USB interrupt. Buffers are whole 32-bit words: one stereo
 * 16-bit frame each. */
typedef void (*gp5_usb_iso_fn)(unsigned ep);
void gp5_usb_iso_in_setup(unsigned ep, unsigned maxp);
void gp5_usb_iso_out_setup(unsigned ep, unsigned maxp);
int gp5_usb_iso_send(unsigned ep, const uint32_t *words, unsigned len);   /* 0: last packet still waiting */
unsigned gp5_usb_iso_receive(unsigned ep, uint32_t *words, unsigned max);
void gp5_usb_iso_irq(unsigned ep, gp5_usb_iso_fn fn);        /* fn 0: off. The USB line is on while any is set. */
void gp5_usb_irq(void);                                      /* the interrupt handler (start/irq.c) */
extern volatile uint32_t gp5_usb_irq_cycles, gp5_usb_irq_count, gp5_usb_irq_worst, gp5_usb_irq_storms;
#endif
