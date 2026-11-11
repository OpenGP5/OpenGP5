/* Runs a firmware's DSP half on the computer: tools/run_effect.py builds this with the firmware's
 * firmware.h and the platform's maths (gp5_math.c), so the arithmetic is the pedal's.
 *   run_effect [cmd-hex ...] < in.raw > out.raw
 * in.raw: int32 samples (the guitar, left-justified); out.raw: int32 left, right pairs. Each cmd-hex is a
 * link payload ([cmd u16][len u16][fields], as gp5_link.py raw takes) handed to fw_command() after
 * fw_init(); its reply or ACK is printed to stderr. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GP5_HOST
#include "gp5_dsp.h"

__attribute__((unused)) static void gp5_reply(const uint8_t *p, uint32_t n)
{
    fprintf(stderr, "reply:");
    for (uint32_t i = 0; i < n; i++) fprintf(stderr, " %02x", p[i]);
    fprintf(stderr, "\n");
}
__attribute__((unused)) static void gp5_ack(void) { fprintf(stderr, "ACK\n"); }
__attribute__((unused)) static uint32_t gp5_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#include "firmware.h"

int main(int argc, char **argv)
{
    static int32_t in[GP5_BLOCK], l[GP5_BLOCK], r[GP5_BLOCK], out[2 * GP5_BLOCK];
    fw_init();
    for (int a = 1; a < argc; a++) {
        uint8_t p[256]; unsigned n = 0;
        for (const char *h = argv[a]; h[0] && h[1] && n < sizeof p; h += 2) { unsigned v; sscanf(h, "%2x", &v); p[n++] = (uint8_t)v; }
        uint32_t cmd = p[0] | (p[1] << 8);
        if (!fw_command(cmd, p, n)) fprintf(stderr, "cmd %04x: not the firmware's (the platform would ACK it)\n", cmd);
    }
    size_t got;
    while ((got = fread(in, sizeof in[0], GP5_BLOCK, stdin)) > 0) {
        if (got < GP5_BLOCK) memset(in + got, 0, (GP5_BLOCK - got) * sizeof in[0]);
        fw_process(in, l, r);
        for (unsigned i = 0; i < GP5_BLOCK; i++) { out[2 * i] = l[i]; out[2 * i + 1] = r[i]; }
        fwrite(out, sizeof out[0], 2 * got, stdout);
    }
    return 0;
}
