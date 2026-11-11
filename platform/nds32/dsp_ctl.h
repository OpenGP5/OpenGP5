#ifndef GP5_DSP_CTL_H
#define GP5_DSP_CTL_H
#include <stdint.h>
#include <stdbool.h>

/* The platform DSP starts muted; it passes audio once it has an output level. dsp_ctl_poll() sends the
 * level once the DSP has answered the version query, and afterwards forwards level changes. */

#define DSP_CTL_STEPS  1
#define DSP_CTL_DONE   1        /* dsp_ctl_step: 0 = level not sent yet, 1 = acknowledged */
#define DSP_CTL_FAILED 2        /* gave up after three rounds */

extern uint8_t dsp_ctl_step;
extern uint32_t dsp_ctl_naks;   /* commands that were not acknowledged (sync and level changes) */

/* Call from the main loop. dsp_ready: the DSP has answered the version query. level: 0..255 (the
 * volume knob), sent as output level level/255. Blocks while a command waits for its ACK (a few ms,
 * at most 300 ms). */
void dsp_ctl_poll(int dsp_ready, unsigned level);

/* Builders (no hardware; also used by the host-side test). Each returns the payload length. */
unsigned dsp_ctl_step_payload(unsigned step, unsigned level, uint8_t *p);
uint32_t dsp_ctl_level_bits(unsigned level);      /* IEEE-754 bits of level/255, without float code */

#endif
