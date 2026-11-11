/* GP-5 open platform: interrupt lines of the control processor. */
#ifndef GP5_IRQ_H
#define GP5_IRQ_H
#include <stdint.h>

enum { GP5_IRQ_TICK = 0, GP5_IRQ_UART0 = 7, GP5_IRQ_TIMER2 = 8, GP5_IRQ_USB = 20, GP5_IRQ_COUNT = 32 };

void gp5_irq_enable(unsigned line);
void gp5_irq_disable(unsigned line);
void gp5_irq_priority(unsigned line, unsigned priority);    /* 0 = highest .. 3 = lowest */
void gp5_irq_global(int on);                                /* all interrupts on / off */
unsigned gp5_irq_global_state(void);

/* Interrupt lines the platform uses */
#define GP5_IRQ_TICK    0     /* timer 1, the 1 ms tick */
#define GP5_IRQ_UART0   7     /* the DSP link */
#define GP5_IRQ_TIMER2  8     /* the LED pulse-width timer */

/* The same controls under the vendor's names (irq.c); negative lines are ignored. */
void GIE_ENABLE(void);
void GIE_DISABLE(void);
int GIE_STATE_GET(void);
void NVIC_EnableIRQ(int line);
void NVIC_DisableIRQ(int line);
void NVIC_SetPriority(int line, uint32_t priority);

/* Lines that fired with no handler: each is switched off the first time and counted here. */
extern volatile uint32_t gp5_irq_stray;
#endif
