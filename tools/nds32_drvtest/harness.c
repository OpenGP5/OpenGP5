/* Differential driver test, run in the Andes instruction simulator (nds32le-elf-run), no hardware.
 * The peripheral space 0x40000000..0x400FFFFF is plain memory here. For each scenario and seed:
 * fill the listed register windows with a pseudo-random pattern, run the scenario (a few driver
 * calls), then print every word of the peripheral space that differs from what was put there.
 * Built twice: against the vendor library and against our own drivers. Equal output for every seed
 * means our code leaves every register in the same state as the vendor's, from any starting state. */
#include <stdio.h>
#include <stdint.h>

#define PBASE 0x40000000u
#define PSIZE 0x00100000u

typedef struct { const char *name; void (*fn)(void); } scenario_t;
extern const scenario_t scenarios[];
extern const unsigned n_scenarios;
extern const uint32_t windows[][2];        /* {start, end} byte addresses to randomise */
extern const unsigned n_windows;

static uint32_t pattern(uint32_t addr, uint32_t seed)
{
    uint32_t x = addr * 2654435761u ^ seed * 0x9E3779B9u;
    if (seed == 0) return 0;
    if (seed == 1) return 0xFFFFFFFFu;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x;
}

static uint32_t expected(uint32_t addr, uint32_t seed)
{
    unsigned i;
    for (i = 0; i < n_windows; i++) if (addr >= windows[i][0] && addr < windows[i][1]) return pattern(addr, seed);
    return 0;
}

int main(void)
{
    static const uint32_t seeds[] = { 0, 1, 2, 3, 4, 5 };
    unsigned s, k, i;
    uint32_t a;
    for (k = 0; k < n_scenarios; k++) {
        for (s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
            for (a = PBASE; a < PBASE + PSIZE; a += 4) *(volatile uint32_t *)a = 0;
            for (i = 0; i < n_windows; i++)
                for (a = windows[i][0]; a < windows[i][1]; a += 4) *(volatile uint32_t *)a = pattern(a, seeds[s]);
            scenarios[k].fn();
            printf("== %s seed %u\n", scenarios[k].name, (unsigned)seeds[s]);
            for (a = PBASE; a < PBASE + PSIZE; a += 4) {
                uint32_t v = *(volatile uint32_t *)a, e = expected(a, seeds[s]);
                if (v != e) printf("%08x: %08x -> %08x\n", (unsigned)a, (unsigned)e, (unsigned)v);
            }
        }
    }
    return 0;
}
