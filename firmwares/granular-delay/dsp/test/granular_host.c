/* Host harness for granular.h: reads "id value" parameter pairs from argv, int32 samples (pedal format)
 * from stdin, writes processed int32 samples to stdout as left, right pairs. A "@N" argument applies
 * the pairs after it at sample N. Buffer sizes as on the pedal. Used by tools/granular_sim.py. */
#include <stdio.h>
#include <stdlib.h>
#include "../granular.h"
#define DLEN 22528
#define LLEN 39936
int main(int argc, char **argv) {
    static int16_t dbuf[DLEN], lbuf[LLEN];
    static gr_t g;
    gr_init(&g, dbuf, DLEN, lbuf, LLEN);
    int ai = 1;
    long pos = 0, next = 0;
    int32_t blk[64], l[64], r[64], io[128];
    for (;;) {
        while (ai < argc && pos >= next) {
            if (argv[ai][0] == '@') { next = atol(argv[ai] + 1); ai++; if (pos < next) break; continue; }
            gr_set(&g, (uint32_t)atoi(argv[ai]), (uint32_t)atoi(argv[ai + 1]));
            ai += 2;
        }
        size_t n = fread(blk, 4, 64, stdin);
        if (!n) break;
        gr_block(&g, blk, l, r, (uint32_t)n);
        for (size_t i = 0; i < n; i++) { io[2 * i] = l[i]; io[2 * i + 1] = r[i]; }
        fwrite(io, 4, 2 * n, stdout);
        pos += (long)n;
    }
    fprintf(stderr, "spawned %u dropped %u\n", g.spawned, g.dropped);
    return 0;
}
