/* GP-5 open platform: memory regions and the instruction / data caches of the NDS32 core.
 * Called from gp5_startup() before .data and .bss exist: only constants and registers here. */
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "cache.h"

/* One protection-unit entry: where the region starts, its size word, and the entry word
 * (physical base | cache mode << 6 | 3 << 4 | access << 1 | valid). */
struct region { uint32_t start, size, entry; };

#define ENTRY(base, cache, access) ((base) | ((cache) << 6) | (3u << 4) | ((access) << 1) | 1u)

static const struct region regions[] = {
    { 0x00000000u, 0x1FFFFFFFu, ENTRY(0x00000000u, 4u, 1u) },   /* flash: cached, read only */
    { 0x20000000u, 0x00048000u, ENTRY(0x20000000u, 5u, 3u) },   /* RAM: cached */
    { 0x40000000u, 0x1FFFFFFFu, ENTRY(0x40000000u, 0u, 3u) },   /* peripherals: never cached */
    { 0x60000000u, 0x1FFFFFFFu, ENTRY(0x60000000u, 5u, 3u) },
    { 0x80000000u, 0x00008000u, ENTRY(0x20048000u, 0u, 3u) },   /* uncached view of RAM at 0x20048000 */
};

void gp5_cache_on(void)
{
    unsigned i;

    for (i = 0; i < sizeof regions / sizeof regions[0]; i++) {
        __nds32__mtsr(regions[i].entry, NDS32_SR_TLB_DATA);
        __nds32__mtsr(regions[i].size, NDS32_SR_TLB_VPN);
        __nds32__dsb();
        __nds32__tlbop_twr(regions[i].start);
        __nds32__isb();
    }
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_PSW) | 0xC0u, NDS32_SR_PSW);          /* translation on */
    __nds32__isb();
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_CACHE_CTL) | 1u, NDS32_SR_CACHE_CTL); /* instruction cache */
    __nds32__isb();
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_CACHE_CTL) | 2u, NDS32_SR_CACHE_CTL); /* data cache */
    __nds32__isb();
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_MMU_CTL) | 6u, NDS32_SR_MMU_CTL);
    __nds32__isb();
}

/* Before handing the CPU to the factory loader: everything off again and both caches emptied,
 * so the loader starts from the state it expects after a reset. */
void gp5_cache_off(void)
{
    uint32_t idx;

    __nds32__mtsr(__nds32__mfsr(NDS32_SR_MMU_CTL) & ~6u, NDS32_SR_MMU_CTL);
    __nds32__isb();
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_PSW) & ~0xC0u, NDS32_SR_PSW);
    __nds32__isb();
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_CACHE_CTL) & ~1u, NDS32_SR_CACHE_CTL);
    __nds32__isb();
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_CACHE_CTL) & ~2u, NDS32_SR_CACHE_CTL);
    __nds32__isb();

    __nds32__cctl_l1d_invalall();
    __nds32__dsb();
    idx = 0x8000u;                                  /* instruction cache: every 32-byte line index */
    do {
        idx -= 32u;
        __nds32__cctlidx_wbinval(NDS32_CCTL_L1I_IX_INVAL, idx);
    } while (idx);
}
