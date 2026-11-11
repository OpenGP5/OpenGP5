#ifndef GP5_PROTO_H
#define GP5_PROTO_H
#include <stdint.h>
#include <stdbool.h>

/* Feed one 4-byte USB-MIDI event packet. Returns PROTO_TO_LOADER when a valid `11 6F` frame has been
 * received and acknowledged; the caller then calls loader_handover(). */
#define PROTO_NONE       0
#define PROTO_TO_LOADER  1
#define PROTO_DSP         2     /* `7E xx` received: request payload in proto_req, the caller answers with proto_reply() */
int proto_feed(const uint8_t *ev);

/* Send one framed message to the host. */
#define PROTO_REPLY_MAX 100
void proto_reply(uint8_t cls, uint8_t index, const uint8_t *pl, unsigned len);

void loader_handover(void);     /* does not return */

extern volatile uint32_t proto_frames, proto_bad;
#define PROTO_REQ_MAX 64        /* longest `7E xx` request payload */
extern uint8_t proto_req[PROTO_REQ_MAX];
extern unsigned proto_req_len;

#endif
