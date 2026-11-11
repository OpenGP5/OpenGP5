#include "timer.h"

#define W(a) (*(volatile uint32_t *)(a))
#define H(a) (*(volatile uint16_t *)(a))
#define CLK_SWITCH 0x40021024u   /* bit 0: core clock from a PLL, bit 1: that PLL is the APLL, bit 13: no system divider */
#define CORE_DIV   0x40021008u   /* core clock = source / (this + 1) */
#define SYS_DIV    0x4002100Cu   /* system clock = core clock / (this + 1) */

static uint32_t base(unsigned timer)
{
    static const uint32_t b[6] = { 0x4002B000u, 0x4002B800u, 0x4002C000u, 0x4002C800u, 0x4002F000u, 0x4002F800u };
    return timer >= 1 && timer <= 6 ? b[timer - 1] : 0;
}

uint32_t gp5_sys_clock_hz(void)
{
    uint32_t sw = W(CLK_SWITCH), hz;
    if (!(sw & 1u)) return 0;
    hz = ((sw & 2u) ? GP5_APLL_HZ : GP5_PLL_HZ) / (W(CORE_DIV) + 1u);
    if (!(sw & (1u << 13))) hz /= W(SYS_DIV) + 1u;
    return hz;
}

int gp5_timer_config(unsigned timer, uint32_t usec, int single_shot)
{
    uint32_t t = base(timer), mhz = gp5_sys_clock_hz() / 1000000u, counts, bits = 0, shift;

    if (!t || !mhz) return -1;
    if (0xFFFFFFFFu / mhz < usec) usec = 0xFFFFFFFFu / mhz;
    counts = mhz * usec;
    while (bits < 32 && (counts >> bits)) bits++;
    shift = bits < 17 ? 0 : bits - 16;                   /* the counter is 16 bits; the rest goes to the prescaler */

    W(t) &= ~0x1u;                                       /* stop */
    W(t) = (W(t) & ~0x2u) | (single_shot ? 0x2u : 0u);
    W(t) |= 0x8u;
    W(t) &= ~0x10u;
    W(t) &= ~0x60u;
    W(t) |= 0x1000u;                                     /* period interrupt on */
    W(t) |= 0x2000u;                                     /* and its flag cleared */
    W(t) &= ~0x100u;
    W(t + 0x0C) = (counts >> shift) - 1u;                /* reload */
    W(t + 0x10) = (1u << shift) - 1u;                    /* prescaler */
    H(t + 0x1C) &= (uint16_t)~0x1E00u;
    H(t + 0x04) &= (uint16_t)~0x7u;
    H(t + 0x04) &= (uint16_t)~0x8u;
    H(t + 0x04) &= (uint16_t)~0x30u;
    H(t + 0x04) &= (uint16_t)~0x40u;
    W(t) |= 0x4u;
    W(t) &= ~0x1u;
    W(t) |= 1u << 24;                                    /* load the new values */
    return 0;
}

void gp5_timer_start(unsigned timer)
{
    uint32_t t = base(timer);
    if (!t) return;
    W(t) |= 0x1u;
    W(t) |= 1u << 24;
}

void gp5_timer_pause(unsigned timer, int pause)
{
    uint32_t t = base(timer);
    if (!t) return;
    if (pause) W(t) &= ~0x1u; else W(t) |= 0x1u;
}

void gp5_timer_int_clear(unsigned timer)
{
    uint32_t t = base(timer);
    if (t) W(t) |= 0x2000u;
}
