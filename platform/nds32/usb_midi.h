#ifndef GP5_USB_MIDI_H
#define GP5_USB_MIDI_H
#include <stdint.h>
#include <stdbool.h>

void usb_midi_init(void);
void usb_midi_poll(void);
int usb_midi_configured(void);
unsigned usb_midi_read(uint8_t *buf);   /* buf: 64 bytes */
int usb_midi_write(uint8_t *buf, unsigned len);

extern volatile uint32_t usb_setups, usb_resets;

#endif
