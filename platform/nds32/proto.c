/* Host command protocol for the GP-5 standalone app: the same SysEx framing the stock firmware and the
 * factory loader use (tools/gp5_flash.py build_sysex / parse_sysex), so the existing tools talk to it.
 *
 *   F0 <nibbles> F7, every byte sent as two nibbles (high first); decoded body:
 *     crc8, class, index, length, payload[length]      crc8 (poly 0x07, init 0) over 00, class, index,
 *                                                      length, payload
 *
 * Commands (class 0x01):
 *   11 6F   reboot into the factory loader's USB update mode. Reply 14 08 00 (the stock ACK), then
 *           loader_handover(). This is what `gp5_flash.py enter --via-6f` sends.
 *   7E xx   (our own, provisional) DSP link commands. The request payload is handed to the caller
 *           (proto_req / proto_req_len, return value PROTO_DSP), which answers with proto_reply():
 *     7E 00                     DSP firmware version       -> 7E 00 <ok> <version u32 LE>
 *     7E 01                     link counters              -> 7E 01 <counters, see main.c> <last unsolicited frame>
 *     7E 02 <cmd lo> <cmd hi> <id>   send a DSP request [cmd][08 00][id u32], wait 100 ms
 *                                                          -> 7E 02 <n> <reply payload, n bytes>
 *     7E 03 <off lo> <off hi> <data, up to 60 bytes>   (FW 7) store data in the raw-payload buffer
 *                                                          -> 7E 03 <ok>
 *     7E 04 <len lo> <len hi> <wait, 10 ms units>      (FW 7) send buffer[0..len) to the DSP as one frame
 *                                                      payload ([cmd][len][fields], any stock command) and
 *                                                      wait for its answer
 *                                                          -> 7E 04 <status> <n> <reply payload, n bytes>
 *                                                      status: 1 = reply frame, 0x14 = ACK, 0x13 = NAK,
 *                                                      0 = no answer
 *     7E 05 <addr u32> <count <= 64>                    read flash below 2 MB (memory-mapped; reads only)
 *                                                          -> 7E 05 <ok> <bytes>
 * Anything else is ignored (counted in proto_frames).
 *
 * loader_handover() is the SDK's start_up_grate(AppResourceUsbDevice) (BT_Audio_APP device/upgrade.c),
 * which is what stock runs for `11 6F` (stock 0x49984, compared instruction by instruction): set bits 1
 * (PC) and 5 (update) in the retained boot-flag register 0x4000100C, tear the chip state down, jump to 0.
 * The loader's boot flow goes to its update session when both bits are set (docs/platform/protocols.md, section 3).
 * It writes no flash. */
#include <nds32_intrinsic.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal/wdg.h"
#include "hal/timer.h"
#include "start/cache.h"
#include "start/irq.h"
#include "hal/clock.h"
#include "usb_midi.h"
#include "proto.h"


#define REG32(a) (*(volatile uint32_t *)(a))
#define BOOT_FLAGS 0x4000100C

volatile uint32_t proto_frames, proto_bad;
uint8_t proto_req[PROTO_REQ_MAX];
unsigned proto_req_len;

static uint8_t nib[2 * (4 + PROTO_REQ_MAX)];   /* nibbles of the frame being received */
static unsigned nnib;
static uint8_t in_frame;

static uint8_t crc8(uint8_t c, const uint8_t *d, unsigned n)
{
    unsigned i;
    while (n--) {
        c ^= *d++;
        for (i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    }
    return c;
}

/* Send one framed message (payload up to PROTO_REPLY_MAX bytes) as USB-MIDI SysEx packets, 64 bytes
 * (16 events) per bulk write. */
void proto_reply(uint8_t cls, uint8_t index, const uint8_t *pl, unsigned len)
{
    static uint8_t body[4 + PROTO_REPLY_MAX], wire[2 + 2 * sizeof body];
    uint8_t pkt[64];
    unsigned i, n = 0, w = 0, left;

    if (len > PROTO_REPLY_MAX) return;
    body[0] = 0; body[1] = cls; body[2] = index; body[3] = (uint8_t)len;
    for (i = 0; i < len; i++) body[4 + i] = pl[i];
    body[0] = crc8(0, body, 4 + len);            /* body[0] is 0 here, as the CRC's leading 00 */

    wire[w++] = 0xF0;
    for (i = 0; i < 4 + len; i++) { wire[w++] = body[i] >> 4; wire[w++] = body[i] & 0x0F; }
    wire[w++] = 0xF7;

    for (i = 0; i < w; i += 3) {
        left = w - i;
        pkt[n] = left > 3 ? 0x04 : (uint8_t)(0x04 + left);     /* 4 = continues; 5/6/7 = ends with 1/2/3 */
        pkt[n + 1] = wire[i];
        pkt[n + 2] = left > 1 ? wire[i + 1] : 0;
        pkt[n + 3] = left > 2 ? wire[i + 2] : 0;
        n += 4;
        if (n == sizeof pkt) { if (!usb_midi_write(pkt, n)) return; n = 0; }
    }
    if (n) usb_midi_write(pkt, n);
}

static int frame_done(void)
{
    uint8_t body[4 + PROTO_REQ_MAX];
    unsigned i, n = nnib / 2, len;
    static const uint8_t ack[3] = { 0x14, 0x08, 0x00 };

    if ((nnib & 1) || n < 4) { proto_bad++; return PROTO_NONE; }
    for (i = 0; i < n; i++) body[i] = (uint8_t)((nib[2 * i] << 4) | nib[2 * i + 1]);
    len = body[3];
    if (4 + len != n) { proto_bad++; return PROTO_NONE; }
    {
        uint8_t got = body[0];
        body[0] = 0;
        if (crc8(0, body, n) != got) { proto_bad++; return PROTO_NONE; }
    }
    proto_frames++;

    if (body[1] == 0x01 && len == 2 && body[4] == 0x11 && body[5] == 0x6F) {
        proto_reply(0x01, 0, ack, 3);
        return PROTO_TO_LOADER;
    }
    if (body[1] == 0x01 && len >= 2 && len <= sizeof proto_req && body[4] == 0x7E) {
        for (i = 0; i < len; i++) proto_req[i] = body[4 + i];
        proto_req_len = len;
        return PROTO_DSP;
    }
    return PROTO_NONE;
}

int proto_feed(const uint8_t *ev)
{
    unsigned cin = ev[0] & 0x0F, count, i;
    int r = PROTO_NONE;

    switch (cin) {
    case 0x4: case 0x7: count = 3; break;
    case 0x6: count = 2; break;
    case 0x5: count = 1; break;
    default:  return PROTO_NONE;                 /* not SysEx */
    }
    for (i = 1; i <= count; i++) {
        uint8_t b = ev[i];
        if (b == 0xF0) { in_frame = 1; nnib = 0; }
        else if (b == 0xF7) { if (in_frame) r = frame_done(); in_frame = 0; }
        else if (in_frame) {
            if (b > 0x0F || nnib >= sizeof nib) { in_frame = 0; proto_bad++; }
            else nib[nnib++] = b;
        }
    }
    return r;
}

void loader_handover(void)
{
    uint32_t flags, a;

    REG32(BOOT_FLAGS) |= 0x02;                   /* PC (USB) update */
    REG32(BOOT_FLAGS) |= 0x20;                   /* update requested */
    flags = REG32(BOOT_FLAGS);

    gp5_wdg_enable_1s();
    __nds32__gie_dis();
    gp5_cache_off();
    NVIC_DisableIRQ(0);                          /* the 1 ms tick: interrupt line off, */
    gp5_timer_int_clear(1);                      /* and its pending flag cleared (as the vendor's tick shut-down did) */

    REG32(0x4000D100) = 0;                       /* DMA */
    for (a = 0x4000D000; a < 0x4000D104; a += 4) REG32(a) = 0;

    REG32(0x40022000) = (REG32(0x40022000) & ~0x0077FFF8u) | 0x007FFFF8u;   /* register reset, not flash/APLL */
    REG32(0x40022004) = (REG32(0x40022004) & ~0x7FFFF7FFu) | 0x7FFFF7FFu;   /* function reset, not flash */

    REG32(BOOT_FLAGS) = flags;
    __nds32__mtsr(0, NDS32_SR_INT_MASK2);
    __nds32__mtsr(0, NDS32_SR_HSP_CTL);
    __asm__("nop");
    gp5_chip_reset_mask();
    ((void (*)(void))0)();
    for (;;) ;
}
