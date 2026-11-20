/* Which register bits does SPIS_DMA_SendRecvState() read? Start from all ones, clear one bit at a time,
 * record the bits whose clearing makes it return 0. Vendor only (a probe, not a difference test). */
#include <stdint.h>
#include "type.h"
#include "spis.h"
#include "dma.h"
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x40000000u, 0x40000000u } };
const unsigned n_windows = 0;
const uint32_t MPUConfigTable[16] = { 0 };
#define REG(a) (*(volatile uint32_t *)(a))
static uint8_t map[] = { 4, 5, 0xff, 0xff, 0xff, 7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 6, 0xff, 0xff, 0xff,
                         0xff, 0, 1, 2, 3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 };
static void fill(void) { uint32_t a; for (a = 0x40004000u; a < 0x40004040u; a += 4) REG(a) = ~0u; for (a = 0x4000d000u; a < 0x4000d200u; a += 4) REG(a) = ~0u; }
static void probe(void)
{
    uint32_t a, b, n = 0;
    DMA_ChannelAllocTableSet(map);
    for (a = 0x40004000u; a < 0x4000d200u; a += 4) {
        if (a == 0x40004040u) a = 0x4000d000u;
        for (b = 0; b < 32; b++) {
            fill(); REG(a) &= ~(1u << b);
            if (!SPIS_DMA_SendRecvState()) REG(0x400FFF00u + 4u * (n++ & 63)) = a | b;   /* address | bit */
        }
    }
    fill(); REG(0x400FFFF0u) = SPIS_DMA_SendRecvState();
    for (a = 0x40004000u; a < 0x40004040u; a += 4) REG(a) = 0; for (a = 0x4000d000u; a < 0x4000d200u; a += 4) REG(a) = 0;
}
const scenario_t scenarios[] = { { "probe", probe } };
const unsigned n_scenarios = 1;
