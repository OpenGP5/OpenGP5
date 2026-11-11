#ifndef GP5_DSP_LINK_H
#define GP5_DSP_LINK_H
#include <stdint.h>
#include <stdbool.h>

#define DSP_CMD_VERSION  0x6000     /* group 6, index 0 */

#define DSP_FRAME_MAX  256           /* longest frame accepted (stock's longest reply is 80 bytes) */
#define DSP_ARGS_MAX   16
#define DSP_UNSOL_MAX  28
#define DSP_RAW_MAX    400           /* longest payload dsp_link_raw() sends (stock's longest: 0x170, the preset) */

void dsp_link_init(void);

/* Call often from the main loop: handles the frames received since the last call. Never waits. */
void dsp_link_poll(void);

/* Send one command frame without waiting for anything. Returns 0 if nargs is too large. */
int dsp_link_send(uint16_t cmd, const uint8_t *args, unsigned nargs);

/* Send one command frame and wait up to `wait_ms` for a reply frame with the same command word.
 * `reply` receives the reply payload (starting with the command word), at most `max` bytes.
 * Returns the payload length, 0 if no valid reply came. */
unsigned dsp_link_query(uint16_t cmd, const uint8_t *args, unsigned nargs,
                        uint8_t *reply, unsigned max, unsigned wait_ms);

/* Send any payload ([cmd u16][len u16][fields], n bytes) as one frame and wait up to `wait_ms` for the
 * DSP's answer: a reply frame with the same command word (copied to `reply`, at most `max` bytes) or a
 * status frame. *status: 1 = reply frame, 0x14 = ACK, 0x13 = NAK, 0 = nothing came (or n out of range).
 * Returns the reply payload length (0 unless *status is 1). */
unsigned dsp_link_raw(const uint8_t *payload, unsigned n, uint8_t *reply, unsigned max, unsigned wait_ms,
                      uint8_t *status);

/* The DSP firmware version (0x01010006 for stock V1.0.6), or 0 if the DSP did not answer. */
uint32_t dsp_link_version(void);

/* Frame helpers (no hardware), also used by the host-side test. */
unsigned dsp_frame_build(uint8_t *out, uint16_t cmd, const uint8_t *args, unsigned nargs);
unsigned dsp_frame_find(const uint8_t *buf, unsigned n, uint16_t cmd, uint8_t *payload, unsigned max);

/* Counters and the last frame the DSP sent that was not the reply to a query. */
extern volatile uint32_t dsp_tx_frames, dsp_rx_bytes, dsp_rx_frames;
extern volatile uint32_t dsp_rx_errs;   /* UART error flags seen (overrun, framing, ...) */
extern volatile uint32_t dsp_rx_lost;   /* bytes dropped because the ring buffer was full */
extern volatile uint32_t dsp_irq_off;   /* 1 = the handler's safety net switched the interrupt off */
extern uint32_t dsp_rx_bad;             /* frames dropped for a bad CRC or for never completing */
extern uint32_t dsp_acks, dsp_naks;     /* status-only frames */
extern uint32_t dsp_unsol_frames;
extern uint16_t dsp_unsol_cmd;
extern uint8_t dsp_unsol[DSP_UNSOL_MAX];
extern unsigned dsp_unsol_len;

#endif
