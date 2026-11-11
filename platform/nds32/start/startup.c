/* GP-5 open platform: what runs between reset (start/vectors.S) and main().
 * Nothing here may use initialised or zeroed variables: .data and .bss are set up last. */
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "irq.h"
#include "cache.h"
#include "hal/clock.h"

extern char __data_lmastart[], __data_start[], _edata[], __bss_start[], _end[];


#define VECTOR_BASE 0x10000u            /* the app slot: vectors are the first words of the image */
#define STACK_LIMIT 0x20003000u         /* the stack is 0x20003000..0x20004000 (4 KB) */

volatile uint32_t gp5_crash[4];         /* written by the exception entry: type, ITYPE, IPC, $sp */

static void cpu_setup(void)
{
    uint32_t v;

    __nds32__mtsr(__nds32__mfsr(NDS32_SR_MMU_CTL) | (1u << 23), NDS32_SR_MMU_CTL);
    __nds32__mtsr(0, NDS32_SR_INT_MASK);                    /* every interrupt line masked */
    __nds32__mtsr(VECTOR_BASE, NDS32_SR_IVB);               /* 4-byte entries, internal vector mode */
    v = __nds32__mfsr(NDS32_SR_PSW);
    __nds32__mtsr((v & ~6u) | 0x70008u, NDS32_SR_PSW);      /* interrupt level 0 */
    __nds32__dsb();
    if (__nds32__mfsr(NDS32_SR_IVB) & 1u) {                 /* external vector controller present */
        __nds32__mtsr(0, NDS32_SR_INT_CTRL);
        if ((__nds32__mfsr(NDS32_SR_IVB) & 0xEu) == 0xAu) {
            __nds32__mtsr(0xFFFFFFFFu, NDS32_SR_INT_PRI);   /* every line at the lowest priority */
            __nds32__mtsr(0xFFFFFFFFu, NDS32_SR_INT_PRI2);
        } else {
            __nds32__mtsr(0xE4u, NDS32_SR_INT_PRI);
        }
    }
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_INT_PEND2), NDS32_SR_INT_PEND2);   /* drop whatever is pending */
}

static void stack_guard(void)
{
    if (!(__nds32__mfsr(NDS32_SR_MSC_CFG) & (1u << 27))) return;            /* core without the guard */
    __nds32__mtsr(__nds32__mfsr(NDS32_SR_HSP_CTL) & ~0xFu, NDS32_SR_HSP_CTL);
    __nds32__mtsr(STACK_LIMIT, NDS32_SR_SP_BOUND);
    __nds32__mtsr(5, NDS32_SR_HSP_CTL);                     /* exception when $sp goes below the limit */
}

void gp5_startup(void)
{
    char *d;
    const char *s;

    cpu_setup();
    gp5_cache_on();
    stack_guard();
    gp5_chip_mem_init();
    for (d = __data_start, s = __data_lmastart; d < _edata; ) *d++ = *s++;
    for (d = __bss_start; d < _end; ) *d++ = 0;
}

/* Vendor objects still linked call printf for debug text (puts is in usb_midi.c). There is no console. */
int printf(const char *f, ...) { (void)f; return 0; }
