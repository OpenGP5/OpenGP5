#ifndef GP5_USB_AUDIO_H
#define GP5_USB_AUDIO_H
#include <stdint.h>
/* USB audio (usb_audio.c): 2 in / 2 out, 16-bit, beside USB-MIDI (descriptors in usb_midi.c), through the audio link. */

#define USB_AUDIO_IF_PLAY 1u        /* audio streaming interfaces (0 = audio control, 3 = MIDI) */
#define USB_AUDIO_IF_REC  2u

void usb_audio_reset(void);
void usb_audio_configured(void);
int usb_audio_set_interface(unsigned intf, unsigned alt);
unsigned usb_audio_get_interface(unsigned intf);
/* The pedal's own sound at the outputs while the computer plays into the pedal (1 = on, the default). Off: only the
 * computer's playback is heard, so a DAW monitoring the pedal doesn't double it. Not kept over a restart. */
void gp5_usb_audio_set_direct(int on);
int gp5_usb_audio_direct(void);
void usb_audio_poll(void);                     /* main loop: passes the setting on to the DSP while the computer plays */
void usb_audio_link_block(const int32_t *in, int32_t *out, uint32_t t0);   /* audio_link.c: every block, after on_block;
                                                                          t0: cycle counter at the handler's start */

extern volatile uint32_t usb_audio_out_packets, usb_audio_in_packets, usb_audio_overruns, usb_audio_underruns,
                         usb_audio_fill, usb_audio_adjust, usb_audio_busy, usb_audio_p_fill, usb_audio_p_under,
                         usb_audio_p_over, usb_audio_r_over, usb_audio_cycles, usb_audio_worst;
extern volatile int32_t usb_audio_ppm;
#endif
