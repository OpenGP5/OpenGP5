/* GP-5 open platform: interrupt control and the handler table (entry code: start/vectors.S). */
#include <nds32_intrinsic.h>
#include "irq.h"

volatile uint32_t gp5_irq_stray = 0;

void gp5_irq_enable(unsigned line)
{
    if (line < GP5_IRQ_COUNT) __nds32__mtsr(__nds32__mfsr(NDS32_SR_INT_MASK2) | (1u << line), NDS32_SR_INT_MASK2);
}

void gp5_irq_disable(unsigned line)
{
    if (line < GP5_IRQ_COUNT) __nds32__mtsr(__nds32__mfsr(NDS32_SR_INT_MASK2) & ~(1u << line), NDS32_SR_INT_MASK2);
}

void gp5_irq_priority(unsigned line, unsigned priority)
{
    uint32_t v;
    unsigned sh;

    priority &= 3u;
    if (line < 16) {
        sh = 2 * line;
        v = __nds32__mfsr(NDS32_SR_INT_PRI);
        __nds32__mtsr((v & ~(3u << sh)) | (priority << sh), NDS32_SR_INT_PRI);
    } else if (line < 32) {
        sh = 2 * (line - 16);
        v = __nds32__mfsr(NDS32_SR_INT_PRI2);
        __nds32__mtsr((v & ~(3u << sh)) | (priority << sh), NDS32_SR_INT_PRI2);
    }
}

void gp5_irq_global(int on)
{
    if (on) { __nds32__setgie_en(); __nds32__dsb(); }
    else { __nds32__setgie_dis(); __nds32__dsb(); __nds32__dsb(); }
}

unsigned gp5_irq_global_state(void) { return __nds32__mfsr(NDS32_SR_PSW) & 1u; }

/* A line with no handler: switch it off so it cannot fire again, and count it. */
static void stray(unsigned line)
{
    gp5_irq_disable(line);
    gp5_irq_stray++;
}

/* The platform's handlers. */
extern void SystickInterrupt(void);     /* hal/tick.c: 1 ms tick (timer 1) */
extern void UART0_Interrupt(void);      /* dsp_link.c: DSP link receive */
extern void Timer2Interrupt(void);      /* led.c: LED brightness */
extern void gp5_link_irq(void);         /* audio_link.c: audio link exchange done (DMA channel 4) */

typedef void (*gp5_irq_fn)(unsigned line);
#define H(f) ((gp5_irq_fn)(void (*)(void))(f))

const gp5_irq_fn gp5_irq_table[GP5_IRQ_COUNT] = {
    [0 ... GP5_IRQ_COUNT - 1] = stray,
    [GP5_IRQ_TICK] = H(SystickInterrupt),
    [GP5_IRQ_UART0] = H(UART0_Interrupt),
    [GP5_IRQ_TIMER2] = H(Timer2Interrupt),
    [13] = H(gp5_link_irq),
};

/* Names the vendor objects that are still linked (USB) call. */
void GIE_ENABLE(void) { gp5_irq_global(1); }
void GIE_DISABLE(void) { gp5_irq_global(0); }
int GIE_STATE_GET(void) { return (int)gp5_irq_global_state(); }
void NVIC_EnableIRQ(int line) { if (line >= 0) gp5_irq_enable((unsigned)line); }
void NVIC_DisableIRQ(int line) { if (line >= 0) gp5_irq_disable((unsigned)line); }
void NVIC_SetPriority(int line, uint32_t priority) { if (line >= 0) gp5_irq_priority((unsigned)line, priority); }
