/* Host test of USB audio's clock following (usb_audio.c), with the USB hardware replaced:
 *   platform/nds32/test/usb_audio_test.sh
 * The computer's clock runs PPM_HOST off nominal and the DSP's PPM_DSP; the computer sends a sine in 1 ms packets and
 * takes recording packets every 1 ms, the DSP exchanges a 64-frame block every 64 / its rate seconds. Checks:
 * recording carries the DSP's frames in order, none lost or repeated, after the start; the playback buffer settles
 * near its target and the correction matches the clocks; and each resampled frame is the sine at the very position
 * the resampler read (its own record of it), within RESAMPLE_DB below the sine (the filter's accuracy, not the
 * steering). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../usb_audio.c"

uint32_t gp5_test_cycles;
volatile uint8_t dsp_ctl_direct_off;                 /* dsp_ctl.c */
#define RESAMPLE_DB 70.0
#define CYC(t) ((uint32_t)(uint64_t)((t) * GP5_CTL_MHZ * 1e6))   /* the cycle counter at time t (it wraps, as on the pedal) */

/* the USB driver and interrupt controls, replaced */
static uint32_t out_pkt[64]; static unsigned out_len;
static uint32_t in_seen, in_bad, in_frames, in_expect, in_started;
unsigned gp5_usb_iso_receive(unsigned ep, uint32_t *w, unsigned max) { (void)ep; unsigned n = out_len < max ? out_len : max; memcpy(w, out_pkt, n); return n; }
int gp5_usb_iso_send(unsigned ep, const uint32_t *w, unsigned len) {
    (void)ep;
    for (unsigned i = 0; i < len / 4; i++) {
        if (!w[i] && !in_started) continue;          /* silence before the stream starts */
        if (!in_started) { in_started = 1; in_expect = w[i]; }
        if (w[i] != in_expect) in_bad++;
        in_expect = w[i] + 1; in_frames++;
    }
    in_seen++; return 1;
}
void gp5_usb_iso_in_setup(unsigned ep, unsigned m) { (void)ep; (void)m; }
void gp5_usb_iso_out_setup(unsigned ep, unsigned m) { (void)ep; (void)m; }
void gp5_usb_iso_irq(unsigned ep, gp5_usb_iso_fn f) { (void)ep; (void)f; }
void gp5_irq_global(int on) { (void)on; }
unsigned gp5_irq_global_state(void) { return 0; }

int main(int argc, char **argv)
{
    double ppm_host = argc > 1 ? atof(argv[1]) : 80, ppm_dsp = argc > 2 ? atof(argv[2]) : -40, secs = 30;
    double fh = 44100.0 * (1 + ppm_host * 1e-6), fd = 44100.0 * (1 + ppm_dsp * 1e-6);
    double t_usb = 0.001, t_dsp = 64.0 / fd;
    uint64_t host_n = 0; uint32_t dsp_n = 1, i, fails = 0, max_fill = 0, min_fill = 1u << 30, blocks = 0;
    FILE *f = fopen(argc > 3 ? argv[3] : "/dev/null", "wb");
    int32_t in[64], out[64];
    double err2[2] = {0, 0}, sig2[2] = {0, 0};
    int gaps = getenv("GAPS") != 0, late = getenv("LATE") != 0;              /* the stream stops twice early on (see below): it must recover */
    uint32_t under_settled = 0;

    usb_audio_configured();
    /* the pedal's own sound: left out only while the computer plays, and only when switched off */
    usb_audio_poll(); if (dsp_ctl_direct_off || !gp5_usb_audio_direct()) fails++;
    gp5_usb_audio_set_direct(0); usb_audio_poll(); if (dsp_ctl_direct_off || gp5_usb_audio_direct()) fails++;
    usb_audio_set_interface(1, 1); usb_audio_poll(); if (!dsp_ctl_direct_off) fails++;
    usb_audio_set_interface(1, 0); usb_audio_poll(); if (dsp_ctl_direct_off) fails++;
    gp5_usb_audio_set_direct(1); usb_audio_set_interface(1, 1); usb_audio_poll(); if (dsp_ctl_direct_off) fails++;
    if (fails) puts("direct switch: wrong");
    usb_audio_set_interface(1, 1);
    usb_audio_set_interface(2, 1);
    for (double now = 0; now < secs; ) {
        if (t_usb <= t_dsp) {                        /* a USB frame: one playback packet, one recording packet */
            now = t_usb; t_usb += 0.001; gp5_test_cycles = CYC(now);
            uint64_t want = (uint64_t)(fh * now);   /* the computer's samples so far */
            if (gaps && ((now >= 5.0 && now < 5.091) || (now >= 7.0 && now < 7.043))) {   /* Windows, a program opening
                                                                 the device: the stream stops 91 ms, then 43 ms */
                host_n = want; rec_packet(4); continue;
            }
            out_len = (unsigned)(want - host_n) * 4u;
            for (i = 0; i < out_len / 4; i++, host_n++) {
                int16_t l = (int16_t)lrint(16000 * sin(2 * M_PI * 1000.0 * host_n / 44100.0));
                int16_t r = (int16_t)lrint(12000 * sin(2 * M_PI * 3000.0 * host_n / 44100.0));
                out_pkt[i] = (uint16_t)l | (uint32_t)(uint16_t)r << 16;
            }
            play_packet(5);
            rec_packet(4);
        } else {                                     /* a DSP block: 64 recorded frames in, 64 playback frames out */
            now = t_dsp; t_dsp += 64.0 / fd;
            /* the handler runs a little later; with LATE, it started 1.1 ms before the packets just taken (a long one,
             * NAM Loader's, with USB interrupts arriving while it runs) */
            gp5_test_cycles = late ? CYC(now - 0.0011) : CYC(now + 0.0002);
            for (i = 0; i < 64; i++) in[i] = (int32_t)dsp_n++;
            uint64_t pos0 = p_pos; int64_t step = 0x100000000LL + p_adj; int primed = p_primed;
            usb_audio_link_block(in, out, gp5_test_cycles);
            if (primed && p_primed && blocks > (uint32_t)(10 * fd / 64))   /* the outputs against the ideal sine */
                for (i = 0; i < 64; i++) {
                    uint64_t q = pos0 + (uint64_t)(step * (int64_t)(i - i % CHUNK)) + ((uint64_t)(i % CHUNK) << 32);
                    double pos = (double)(q >> 32) + (double)(uint32_t)q / 4294967296.0;
                    double l = 16000 * sin(2 * M_PI * 1000.0 * pos / 44100.0), r = 12000 * sin(2 * M_PI * 3000.0 * pos / 44100.0);
                    double el = (int16_t)out[i] - l, er = (int16_t)(out[i] >> 16) - r;
                    err2[0] += el * el; sig2[0] += l * l; err2[1] += er * er; sig2[1] += r * r;
                }
            fwrite(out, 4, 64, f);
            if (blocks == (uint32_t)(10 * fd / 64)) under_settled = usb_audio_p_under;
            if (++blocks > (uint32_t)(10 * fd / 64)) {     /* after 10 s: settled */
                if (usb_audio_p_fill > max_fill) max_fill = usb_audio_p_fill;
                if (usb_audio_p_fill < min_fill) min_fill = usb_audio_p_fill;
            }
        }
    }
    fclose(f);
    printf("host %+.0f ppm, DSP %+.0f ppm: playback fill %u..%u (target %u), correction %+d ppm (expect %+.0f), "
           "underruns %u, overruns %u; recording %u frames, %u out of order, %u underruns, %u overruns\n",
           ppm_host, ppm_dsp, min_fill, max_fill, (unsigned)P_TARGET, (int)usb_audio_ppm, ppm_host - ppm_dsp,
           (unsigned)usb_audio_p_under, (unsigned)usb_audio_p_over, in_frames, in_bad, (unsigned)usb_audio_underruns,
           (unsigned)usb_audio_r_over);
    printf("  resampled sines against the ideal: 1 kHz %.1f dB, 3 kHz %.1f dB below the sine\n",
           10 * log10(sig2[0] / err2[0]), 10 * log10(sig2[1] / err2[1]));
    /* (with gaps, the frames the computer dropped shift the numbering the ideal sine is drawn from: not comparable) */
    if (!gaps && (10 * log10(sig2[0] / err2[0]) < RESAMPLE_DB || 10 * log10(sig2[1] / err2[1]) < RESAMPLE_DB)) fails++;
    if (gaps) printf("  stream gaps at 5 s and 7 s: %u dry-outs before 10 s, %u after\n", (unsigned)under_settled,
                     (unsigned)(usb_audio_p_under - under_settled));
    if (in_bad || usb_audio_p_under - (gaps ? under_settled : 0) || usb_audio_p_over || usb_audio_r_over || in_frames < secs * fd * 0.95) fails++;
    {   /* how far the fill strays while the loop settles grows with the clocks' difference (far-off ones: 0.5 % max) */
        double d = fabs(ppm_host - ppm_dsp); unsigned m = 80 + (unsigned)(d / 12);
        if (max_fill > P_TARGET + m || min_fill + m < P_TARGET || max_fill + HALF + MAXF >= RING) fails++;
    }
    if (fabs(usb_audio_ppm - (ppm_host - ppm_dsp)) > (gaps ? 25 : 10) + fabs(ppm_host - ppm_dsp) / 100) fails++;   /* 1 % of
                                                   it; after gaps the steering started again from 0 and is still settling */
    puts(fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
