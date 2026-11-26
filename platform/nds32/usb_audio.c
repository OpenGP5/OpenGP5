/* GP-5 open platform: USB audio (class compliant, UAC1): 2 channels in, 2 out, 16-bit, at the firmware's sample
 * rate, beside USB-MIDI in one composite device (descriptors: usb_midi.c). On unless firmware.json says
 * "usb_audio": false (platform/manifest.py).
 *
 * The sound travels in the platform's 64 words of the audio link (audio_link.c; the DSP side is platform/dsp/audio.h),
 * one stereo 16-bit frame per word, as on the USB wire:
 * - Playback (computer -> pedal): isochronous OUT 0x05, adaptive: the computer sends at its own clock, 44 / 45 (or 48)
 *   frames a millisecond. The DSP runs on its crystal, 64 frames a block. A resampler in the link's handler takes the
 *   computer's frames to the DSP's rate (polyphase windowed sinc, usb_resample.h), its ratio steered by how full its
 *   buffer is (about 2 ppm per frame off the target, filtered): the computer's clock and the DSP's differ by tens of ppm.
 *   The DSP mixes the frames into its outputs, before the volume knob.
 * - Recording (pedal -> computer): isochronous IN 0x84, asynchronous: the DSP's frames go out untouched, 44 / 45 (48)
 *   a millisecond, one more or one fewer when the buffer drifts from its target, so the computer follows the DSP's
 *   clock. The frames: the firmware's finished output, from the DSP (64 words back), or handed over on this processor
 *   (gp5_usb_audio_record(), "record_from": "control").
 * Both endpoints are served from the USB interrupt (hal/usb.c), one packet a millisecond each. */
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "hal/usb.h"
#include "start/irq.h"
#include "usb_audio.h"
#include "usb_resample.h"
#include "dsp_ctl.h"
#include "gp5_firmware.h"

#define EP_PLAY   0x05                          /* host -> pedal */
#define EP_REC    0x84                          /* pedal -> host */
#define RATE      GP5_SAMPLE_RATE
#define NOMINAL   (RATE / 1000u)                 /* 48 or 44 frames a millisecond */
#define MAXF      (NOMINAL + 1u)                 /* frames in the largest packet */
#define RING      512u                           /* playback frames (power of two) */
#define RRING     256u                           /* recording frames: the fill peaks near R_TARGET + a block (~220) */
#define HALF      (RS_TAPS / 2u)                 /* the filter reaches HALF - 1 frames back, HALF ahead */
#define P_TARGET  (GP5_BLOCK + HALF + 2u * NOMINAL)   /* playback: frames waiting after a block is taken (~2 ms spare) */
#define R_TARGET  (GP5_BLOCK + 2u * NOMINAL)     /* recording: frames waiting before a packet is taken */
#define WIN       (GP5_BLOCK + RS_TAPS + 2u)    /* frames one block's filter reads (one of drift, one spare) */
#define CHUNK     4u                             /* outputs per filter phase (it moves < 0.1 % of a frame per output) */
/* Rate steering, a PI loop on the fill (filtered over ~128 blocks): 10 ppm per frame off the target, plus its sum
 * (1.1 ppm per frame-second): critically damped (1 ppm moves the fill 0.044 frames a second), settles in a few
 * seconds, and the fill ends on the target whatever the clocks' difference (host test: usb_audio_test.sh). */
#define KP        168                            /* Q32 step per Q8 frame of error: 10 ppm a frame */
#define KI        7025                           /* (sum of Q8 errors, one a block) * KI >> 18: 1.1 ppm per frame-second at 689 blocks/s */
#define DUE_K     ((uint32_t)(((uint64_t)RATE * 256u << 32) / (GP5_CTL_MHZ * 1000000u)))   /* Q8 frames per cycle, Q32 */
#define ADJ_MAX   8590000                        /* at most 2000 ppm: real clocks are a few hundred apart (WIN allows
                                                    15,000: a frame a block) */

/* in the low RAM (app.ld .lowram: 0x20000000..0x20003000, the vendor SDK's flash-driver area, unused by this platform;
 * not cleared at start-up): platform buffers there leave the firmware's RAM alone */
#if defined(__nds32__)
#define LOWRAM __attribute__((section(".lowram")))
#else
#define LOWRAM                                 /* the computer (tests, simulator): ordinary memory */
#endif
static LOWRAM uint32_t pring[RING], rring[RRING];        /* one stereo 16-bit frame per word, as on the wire */
static volatile uint32_t p_head, p_tail;         /* playback: written by the USB interrupt / by the link's handler */
static volatile uint32_t r_head, r_tail;         /* recording: written by the link's handler / by the USB interrupt */
static uint64_t p_pos;                           /* playback read position, frames in Q32 (link handler only) */
static int32_t p_adj, p_avg;                     /* step - 1.0 (Q32), filtered fill (Q8) */
static int64_t p_sum;                            /* the steering's sum of errors (Q8 frames, one a block) */
static volatile uint32_t p_tpkt;                 /* cycle counter when the last playback packet arrived */
static int32_t r_avg;
static uint32_t pkt[MAXF + 1];
static volatile uint8_t play_on, rec_on;
static uint8_t p_primed, r_primed;
static uint32_t frac;                            /* 44.1 kHz: thousandths of a frame */

volatile uint32_t usb_audio_out_packets, usb_audio_in_packets, usb_audio_overruns, usb_audio_underruns,
                  usb_audio_fill, usb_audio_adjust, usb_audio_busy,
                  usb_audio_p_fill, usb_audio_p_under, usb_audio_p_over, usb_audio_r_over,
                  usb_audio_cycles, usb_audio_worst;
volatile int32_t usb_audio_ppm;                  /* the playback resampler's correction, ppm (for a look) */

/* ---- playback ---- */

#ifdef GP5_USB_DIAG
/* test builds: what the computer's playback packets look like (usb_audio_poll logs it every 5 s) */
static volatile uint32_t d_min = 0xFFFF, d_max, d_empty, d_gap, d_pkts, d_frames, d_fmin = 0xFFFF, d_fmax, d_dry0;
#endif
static void play_packet(unsigned ep)             /* a playback packet arrived (USB interrupt) */
{
    unsigned len = gp5_usb_iso_receive(ep, pkt, sizeof pkt), n = len >> 2, i;
    uint32_t h = p_head;

    usb_audio_out_packets++;
#ifdef GP5_USB_DIAG
    {   uint32_t now = __nds32__mfsr(NDS32_SR_PFMC0), g = now - p_tpkt;
        if (n < d_min) d_min = n;
        if (n > d_max) d_max = n;
        if (!n) d_empty++;
        if (d_pkts && g > d_gap) d_gap = g;
        d_pkts++; d_frames += n;
        if (!n) p_tpkt = now;
    }
#endif
    if (h + n - (p_tail - HALF) > RING) { usb_audio_p_over++; return; }   /* full: the frames the filter reads stay */
    for (i = 0; i < n; i++) pring[(h + i) & (RING - 1u)] = pkt[i];
    p_tpkt = __nds32__mfsr(NDS32_SR_PFMC0);
    p_head = h + n;
}

static inline __attribute__((always_inline)) int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }

/* (built for speed: -Os made the filter's loops slow) */
/* 64 frames at the DSP's rate into out[] (the link's handler, once a block). t0: the cycle counter when the block's
 * exchange finished (the start of the link's handler: a fixed time after the DSP's block). */
__attribute__((optimize("O2"))) static void play_block(int32_t *out, uint32_t t0)
{
    uint32_t i, j, n, avail;

    if (!play_on) { for (i = 0; i < GP5_BLOCK; i++) out[i] = 0; return; }
    n = (uint32_t)(p_pos >> 32);
    avail = p_head - n;
    if (!p_primed) {                             /* starting: silence until the target is waiting */
        for (i = 0; i < GP5_BLOCK; i++) out[i] = 0;
        if (avail >= P_TARGET + GP5_BLOCK) {
            p_pos = (uint64_t)(p_head - P_TARGET - GP5_BLOCK) << 32;
            p_primed = 1; p_avg = (int32_t)(P_TARGET << 8);   /* p_adj and p_sum carry on: the clocks didn't change */
        }
        p_tail = (uint32_t)(p_pos >> 32);
        return;
    }
    if (avail < WIN - (HALF - 1u)) {             /* ran dry: silence, start again */
        for (i = 0; i < GP5_BLOCK; i++) out[i] = 0;
        p_primed = 0; usb_audio_p_under++;
        /* and the steering from nothing: a correction that ran the buffer dry is wrong (it was learnt across a pause in
         * the stream, say); kept, it ran it dry again every few seconds (Windows, a program opening the device) */
        p_adj = 0; p_sum = 0;
        return;
    }
    /* The frames this block reads (the filter's reach around 64 outputs, at most one frame of drift), unpacked once into
     * left / right arrays: the filter loops then run on plain arrays (4.9 % of a block when every tap read the ring
     * and unpacked its frame, on the pedal 2026-11-23). */
    {   /* each channel twice: from the first frame, and from the second, so a pair of samples starting at any
         * frame is one aligned word: the DSP extension's kmada multiplies two pairs and adds both products */
        static LOWRAM int16_t xl[2][WIN + 1] __attribute__((aligned(4))), xr[2][WIN + 1] __attribute__((aligned(4)));
        const uint32_t s = n - (HALF - 1u);
        for (j = 0; j < WIN; j++) {
            uint32_t x = pring[(s + j) & (RING - 1u)];
            xl[0][j] = (int16_t)x; xr[0][j] = (int16_t)(x >> 16);
            if (j) { xl[1][j - 1] = (int16_t)x; xr[1][j - 1] = (int16_t)(x >> 16); }
        }
        for (i = 0; i < GP5_BLOCK; i += CHUNK) {
            uint32_t f = (uint32_t)p_pos, p = f >> (32u - RS_PHASE_BITS), mu = (f >> (16u - RS_PHASE_BITS)) & 0xFFFFu, k;
            uint32_t c[RS_TAPS / 2];                 /* coefficient pairs, low half first */
            const int16_t *a = rs_table[p], *b = rs_table[p + 1u];
            for (j = 0; j < RS_TAPS / 2; j++) {
                int32_t c0 = a[2 * j] + (((int32_t)b[2 * j] - a[2 * j]) * (int32_t)mu >> 16);
                int32_t c1 = a[2 * j + 1] + (((int32_t)b[2 * j + 1] - a[2 * j + 1]) * (int32_t)mu >> 16);
                c[j] = (uint32_t)(uint16_t)c0 | (uint32_t)c1 << 16;
            }
            /* the chunk keeps its first position's phase and steps whole frames: the phase moves less than 0.1 % of a
             * frame within it. (Taking the position afresh each time jumped a frame when its fraction wrapped.) */
            const uint32_t n0 = (uint32_t)(p_pos >> 32) - n;
            for (k = 0; k < CHUNK; k++) {
                const uint32_t o = n0 + k, h = o & 1u;
                const uint32_t *pl = (const uint32_t *)&xl[h][o - h], *pr = (const uint32_t *)&xr[h][o - h];
                int32_t l = 16384, r = 16384;
                for (j = 0; j < RS_TAPS / 2; j++) { l = __nds32__kmada(l, c[j], pl[j]); r = __nds32__kmada(r, c[j], pr[j]); }
                out[i + k] = (int32_t)((uint16_t)sat16(l >> 15) | (uint32_t)(uint16_t)sat16(r >> 15) << 16);
                p_pos += (uint64_t)((int64_t)0x100000000LL + p_adj);
            }
        }
    }
    n = (uint32_t)(p_pos >> 32);
    p_tail = n;
    /* steer: filtered fill against the target. The fill counts the frames due since the last packet arrived (at the
     * nominal rate, from the cycle counter), and the read position's fraction: read raw at each block it would follow
     * a sawtooth (packets of 44 / 45 frames every ms against blocks of 64 every 1.45 ms) that the filter can't take
     * out completely, and the ratio would wobble (+-6 Hz side tones around a sine at -75 dB, host test). */
    {   uint32_t th, h, dt;
        int32_t fill;
        do { th = p_tpkt; h = p_head; } while (th != p_tpkt);       /* (a packet may come in between) */
        dt = t0 - th;
        if ((int32_t)dt < 0) dt = 0;             /* the packet came after the handler started (a firmware whose handler
                                                    runs over 1 ms, NAM Loader): none is due yet. Unsigned, it read as
                                                    a pause, and the fill jumped by 2 ms as packets and blocks drifted
                                                    past each other (a glitch every few seconds on Windows) */
        if (dt > GP5_CTL_MHZ * 2000u) {          /* packets stopped (the computer paused the stream): hold the steering,
                                                    learning nothing from the pause */
            usb_audio_p_fill = h - n;
            return;
        }
        fill = (int32_t)((h - n) << 8) - (int32_t)((uint32_t)p_pos >> 24)
             + (int32_t)(((uint64_t)dt * DUE_K) >> 32);
        usb_audio_p_fill = (uint32_t)(fill >> 8);
        int32_t e;
        int64_t a, i_term;
        p_avg += (fill - p_avg) >> 7;
        e = p_avg - (int32_t)(P_TARGET << 8);
        p_sum += e;
        i_term = (p_sum * KI) >> 18;
        if (i_term > ADJ_MAX) { i_term = ADJ_MAX; p_sum = ((int64_t)ADJ_MAX << 18) / KI; }      /* no wind-up */
        else if (i_term < -ADJ_MAX) { i_term = -ADJ_MAX; p_sum = -((int64_t)ADJ_MAX << 18) / KI; }
        a = (int64_t)e * KP + i_term;
        p_adj = a > ADJ_MAX ? ADJ_MAX : a < -ADJ_MAX ? -ADJ_MAX : (int32_t)a;
        usb_audio_ppm = (int32_t)(((int64_t)p_adj * 1000000) >> 32);
    }
}

/* ---- recording ---- */

static void rec_push(const uint32_t *frames)    /* 64 frames from the DSP or the firmware (the link's handler) */
{
    uint32_t h = r_head, i;
#ifdef GP5_USB_REC_RAMP
    /* test builds (GP5_USB_REC_RAMP=1): the recording carries a frame counter instead of the sound, so the computer can
     * check that every frame arrives once, in order (work/usb-audio/ramp_check.py) */
    static uint32_t ramp_buf[GP5_BLOCK], ramp;
    for (i = 0; i < GP5_BLOCK; i++) ramp_buf[i] = ramp++;
    frames = ramp_buf;
#endif
    if (!rec_on) return;
    if (h + GP5_BLOCK - r_tail > RRING) {        /* full: this block is dropped. Only counted once sending has begun: */
        if (r_primed) usb_audio_r_over++;        /* at a stream's start the computer begins reading a little late */
        return;
    }
    for (i = 0; i < GP5_BLOCK; i++) rring[(h + i) & (RRING - 1u)] = frames[i];
    r_head = h + GP5_BLOCK;
}

#ifdef GP5_USB_RECORD_CONTROL
static uint8_t rec_given;                        /* the firmware handed this block over */
void gp5_usb_audio_record(const int32_t *left, const int32_t *right)
{
    uint32_t f[GP5_BLOCK], i;
    if (rec_given) return;                       /* once a block */
    rec_given = 1;
    for (i = 0; i < GP5_BLOCK; i++) f[i] = ((uint32_t)left[i] >> 16) | ((uint32_t)right[i] & 0xFFFF0000u);
    rec_push(f);
}
#endif

static void rec_packet(unsigned ep)              /* the last recording packet was taken: queue the next */
{
    uint32_t t = r_tail, fill = r_head - t, n = NOMINAL, i, f = frac;

    if (RATE % 1000u) { f += RATE % 1000u; if (f >= 1000u) { f -= 1000u; n++; } }   /* 44.1: 45 every 10th */
    usb_audio_fill = fill;
    if (!r_primed) {                             /* starting: silence until the target is waiting */
        if (fill >= R_TARGET) { r_primed = 1; r_avg = (int32_t)(fill << 8); }
        for (i = 0; i < n; i++) pkt[i] = 0;
    } else {
        r_avg += (((int32_t)fill << 8) - r_avg) >> 4;
        if (r_avg > (int32_t)((R_TARGET + NOMINAL / 2u) << 8) && n < MAXF) { n++; usb_audio_adjust++; }
        else if (r_avg < (int32_t)((R_TARGET - NOMINAL / 2u) << 8) && n >= NOMINAL) { n--; usb_audio_adjust++; }
        for (i = 0; i < n; i++) {
            if (t == r_head) { pkt[i] = 0; usb_audio_underruns++; r_primed = 0; continue; }
            pkt[i] = rring[t & (RRING - 1u)];
            t++;
        }
    }
    if (!gp5_usb_iso_send(ep, pkt, n * 4u)) { usb_audio_busy++; return; }   /* last one still waiting: next time */
    usb_audio_in_packets++;
    r_tail = t; frac = f;
}

/* ---- the link's handler, once a block, after the firmware's on_block ---- */

void usb_audio_link_block(const int32_t *in, int32_t *out, uint32_t t0)
{
    uint32_t c0 = __nds32__mfsr(NDS32_SR_PFMC0), c;
#ifndef GP5_USB_RECORD_CONTROL
    rec_push((const uint32_t *)in + GP5_USB_REC_AT);
#else
    (void)in;
    if (!rec_given) {                            /* a block the firmware didn't hand over (it skipped one): silence, */
        static const uint32_t zero[GP5_BLOCK];   /* so the recording keeps its place */
        rec_push(zero);
    }
    rec_given = 0;
#endif
    play_block(out + GP5_USB_PLAY_AT, t0);
    c = __nds32__mfsr(NDS32_SR_PFMC0) - c0;
    usb_audio_cycles = c;
    if (c > usb_audio_worst) usb_audio_worst = c;
}

/* ---- USB requests (main loop) ---- */

/* Nothing of USB audio's is touched on the USB controller until the computer starts a stream (SET_INTERFACE, below,
 * sets its endpoint up): an idle pedal's USB is as without USB audio. (Tried for NAM Loader's Bluetooth drops,
 * 2026-11-23: not their cause, but kept: less to go wrong.) */
void usb_audio_reset(void)                       /* bus reset or deconfigured */
{
    if (play_on) gp5_usb_iso_irq(EP_PLAY & 0xF, 0);
    if (rec_on) gp5_usb_iso_irq(EP_REC & 0xF, 0);
    play_on = rec_on = 0;
}

void usb_audio_configured(void)                  /* SET_CONFIGURATION: both interfaces start at alternate 0 */
{
    usb_audio_reset();
}

/* SET_INTERFACE for interface 1 (playback) or 2 (recording). Returns 0 for an interface that isn't audio. */
int usb_audio_set_interface(unsigned intf, unsigned alt)
{
    unsigned i, on;
    if (intf == USB_AUDIO_IF_PLAY) {
        gp5_usb_iso_irq(EP_PLAY & 0xF, 0);
        on = gp5_irq_global_state(); gp5_irq_global(0);         /* the link's handler reads these */
        play_on = 0; p_head = p_tail = 0; p_pos = 0; p_primed = 0;
        for (i = 0; i < RING; i++) pring[i] = 0;
        if (on) gp5_irq_global(1);
        gp5_usb_iso_out_setup(EP_PLAY, 288);
        if (alt == 1) { play_on = 1; gp5_usb_iso_irq(EP_PLAY & 0xF, play_packet); }
        return 1;
    }
    if (intf == USB_AUDIO_IF_REC) {
        gp5_usb_iso_irq(EP_REC & 0xF, 0);
        on = gp5_irq_global_state(); gp5_irq_global(0);
        rec_on = 0; r_head = r_tail = 0; r_primed = 0; frac = 0;
        if (on) gp5_irq_global(1);
        gp5_usb_iso_in_setup(EP_REC, MAXF * 4u);
        if (alt == 1) {
            rec_on = 1;
            gp5_usb_iso_irq(EP_REC & 0xF, rec_packet);
            gp5_usb_iso_send(EP_REC, pkt, 0);   /* an empty packet starts the chain of interrupts */
        }
        return 1;
    }
    return 0;
}

static uint8_t direct_off;
void gp5_usb_audio_set_direct(int on) { direct_off = !on; }
int gp5_usb_audio_direct(void) { return !direct_off; }
void usb_audio_poll(void)
{
    dsp_ctl_direct_off = direct_off && play_on;  /* nothing playing: the pedal's own sound */
#ifdef GP5_USB_DIAG
    {   static uint32_t next;
        uint32_t f = usb_audio_p_fill;
        if (play_on) { if (f < d_fmin) d_fmin = f; if (f > d_fmax) d_fmax = f; }
        if ((int32_t)(gp5_ms() - next) >= 0) {
            next = gp5_ms() + 5000u;
            if (play_on && d_pkts)
                gp5_log("usb play: %u pkts %u frames, size %u..%u, %u empty, gap max %u us; fill %u..%u; dry +%u; %d ppm",
                        (unsigned)d_pkts, (unsigned)d_frames, (unsigned)d_min, (unsigned)d_max, (unsigned)d_empty,
                        (unsigned)(d_gap / GP5_CTL_MHZ), (unsigned)d_fmin, (unsigned)d_fmax,
                        (unsigned)(usb_audio_p_under - d_dry0), (int)usb_audio_ppm);
            d_min = 0xFFFF; d_max = 0; d_empty = 0; d_gap = 0; d_pkts = 0; d_frames = 0; d_fmin = 0xFFFF; d_fmax = 0;
            d_dry0 = usb_audio_p_under;
        }
    }
#endif
}

unsigned usb_audio_get_interface(unsigned intf)
{
    return intf == USB_AUDIO_IF_PLAY ? play_on : intf == USB_AUDIO_IF_REC ? rec_on : 0;
}
