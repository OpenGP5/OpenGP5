/* Start-up and output level for the platform's DSP firmware (platform/dsp/responder.c).
 *
 * The DSP starts muted and passes audio once it has an output level: command 0x1001, sub 0, value = an
 * IEEE-754 float 0.0 .. 1.0, acknowledged with 01 04 00 14. dsp_ctl_poll() sends it once the DSP has
 * answered the version query, then again whenever the level (the volume knob) changes. All float values
 * are built as bit patterns: this file has no floating-point code. */
#include <stdint.h>
#include <stdbool.h>
#include "hal/tick.h"
#include "dsp_link.h"
#include "dsp_ctl.h"

uint8_t dsp_ctl_step;
uint32_t dsp_ctl_naks;

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

uint32_t dsp_ctl_level_bits(unsigned level)
{
    uint32_t q;
    unsigned k = 0;

    if (level == 0) return 0;
    if (level >= 255) return 0x3F800000;                 /* 1.0 */
    q = (level << 24) / 255;                             /* level/255 in Q24, below 2^24 */
    while (!(q & 0x800000)) { q <<= 1; k++; }            /* value = (q / 2^23) * 2^(-1-k) */
    return ((uint32_t)(126 - k) << 23) | (q & 0x7FFFFF);
}

unsigned dsp_ctl_step_payload(unsigned step, unsigned level, uint8_t *p)
{
    (void)step;                                                    /* one command: the output level */
    put16(p, 0x1001); put16(p + 2, 0x0C); put32(p + 4, 0); put32(p + 8, dsp_ctl_level_bits(level));
    return 0x0C;
}

static int send_step(unsigned step, unsigned level)
{
    static uint8_t buf[0x0C];
    uint8_t rep[8], st;

    dsp_link_raw(buf, dsp_ctl_step_payload(step, level, buf), rep, sizeof rep, 300, &st);
    if (st == 0x14) return 1;
    dsp_ctl_naks++;
    return 0;
}

void dsp_ctl_poll(int dsp_ready, unsigned level)
{
    static uint32_t next;
    static uint8_t rounds, started;
    static unsigned sent_level;
    uint32_t now = gp5_ms();

    if (!dsp_ready || dsp_ctl_step == DSP_CTL_FAILED) return;
    if (!started) { started = 1; next = now + 300; }               /* lets the knob reading settle */
    if ((int32_t)(now - next) < 0) return;

    if (dsp_ctl_step < DSP_CTL_DONE) {
        if (send_step(dsp_ctl_step, level)) {
            sent_level = level;
            next = gp5_ms() + 30;
            dsp_ctl_step++;
        } else if (++rounds >= 3) dsp_ctl_step = DSP_CTL_FAILED;
        else { dsp_ctl_step = 0; next = gp5_ms() + 1000; }
        return;
    }
    if (level != sent_level) {
        send_step(0, level);
        sent_level = level;                                        /* not repeated if it was not acknowledged */
        next = gp5_ms() + 20;
    }
}
