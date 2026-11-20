/* The simulator's DSP half (tools/simulate.py): a firmware's dsp/firmware.h built for the computer as a shared
 * library. Only the sim_dsp_* functions below are visible, so the DSP half's own names (fw_init, sinf, ...) never
 * meet the control half's in the same program. The arithmetic is the pedal's (the platform's gp5_math.c). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifndef GP5_HOST
#define GP5_HOST
#endif
#include "gp5_dsp.h"
#include "gp5_fmt.h"

#define EXPORT __attribute__((visibility("default")))
static const uint32_t *clock_ms;                     /* the simulation's time, for log lines */

__attribute__((unused)) static void gp5_reply(const uint8_t *p, uint32_t n)
{
    fprintf(stderr, "dsp reply:");
    for (uint32_t i = 0; i < n; i++) fprintf(stderr, " %02x", p[i]);
    fprintf(stderr, "\n");
}
__attribute__((unused)) static void gp5_ack(void) {}
static void gp5_log(const char *fmt, ...)
{
    char line[112];
    va_list ap;
    uint32_t ms = clock_ms ? *clock_ms : 0u;
    va_start(ap, fmt); gp5_vfmt(line, sizeof line, fmt, ap); va_end(ap);   /* the pedal's printf, not the computer's */
    fprintf(stderr, "[%5u.%03u] dsp: %s\n", ms / 1000u, ms % 1000u, line);
}
__attribute__((unused)) static uint32_t gp5_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* the platform's own counters some firmwares report (the link's status block, audio.h): zeros here */
static __attribute__((unused)) uint32_t ST[64], link_lag;
#include "firmware.h"
#ifdef GP5_HAS_PARAMS
int32_t gp5_param_values[GP5_NPARAMS];
#endif

EXPORT unsigned sim_dsp_link_words(void) { return GP5_LINK_WORDS; }
EXPORT unsigned sim_dsp_sample_rate(void) { return GP5_SAMPLE_RATE; }
EXPORT int32_t *sim_dsp_link(int which) { return gp5_host_link[which ? 1 : 0]; }   /* 0: words in, 1: words out */
EXPORT uint32_t sim_dsp_cycles_last(void) { return GP5_CYCLES_LAST; }

EXPORT void sim_dsp_init(const uint32_t *ms)
{
    clock_ms = ms;
#ifdef GP5_HAS_PARAMS
    for (unsigned k = 0; k < GP5_NPARAMS; k++) gp5_param_values[k] = gp5_param_table[k].def;
#endif
    fw_init();
}

/* a link command from the control half (payload [cmd u16][len u16][fields]); 1 if the firmware took it */
EXPORT int sim_dsp_command(const uint8_t *p, unsigned n)
{
    uint32_t cmd = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
#ifdef GP5_HAS_PARAMS
    if (cmd == 0x7F10 && n >= 12) {                  /* a parameter, as responder.c takes it */
        uint32_t id = gp5_get32(p + 4);
        int32_t v = (int32_t)gp5_get32(p + 8);
        if (id < GP5_NPARAMS) {
            const gp5_param_desc_t *d = &gp5_param_table[id];
            v = v < d->min ? d->min : v > d->max ? d->max : v;
            gp5_param_values[id] = v; fw_param(id, v);
        }
        return 1;
    }
#endif
    return cmd >= 0x7F10 && fw_command(cmd, p, n);
}

EXPORT void sim_dsp_block(const int32_t *in, int32_t *l, int32_t *r) { fw_process(in, l, r); }

/* the early exchange (manifest "link_early": audio.h): 1 if the firmware uses it; then, at the start of each block,
 * fw_link_early() may put words straight into what goes out (the rest came from the last fw_process) */
#ifdef GP5_LINK_EARLY
static void fw_link_early(const int32_t *in, int32_t *out);
EXPORT int sim_dsp_link_early(const int32_t *in) { if (in) fw_link_early(in, gp5_host_link[1]); return 1; }
#else
EXPORT int sim_dsp_link_early(const int32_t *in) { (void)in; return 0; }
#endif
