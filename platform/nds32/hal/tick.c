#include "tick.h"
#include "timer.h"

/* The counter has the vendor library's name because its chip-init object refers to it (see the end of
 * this file); everything of ours reads it through gp5_ms(). */
volatile uint32_t gSysTick = 0;      /* "= 0" matters: a tentative definition would still pull in the vendor's object */
#define ms_count gSysTick

void gp5_tick_setup(void)
{
    gp5_timer_config(1, 1000, 0);
    gp5_timer_start(1);
}

uint32_t gp5_ms(void) { return ms_count; }

void gp5_timeout_set(gp5_timeout_t *t, uint32_t ms)
{
    t->start = ms_count;
    t->deadline = ms_count + ms;
    t->expired = 0;
}

int gp5_timeout_expired(gp5_timeout_t *t)
{
    if (!t->expired && t->deadline < ms_count) t->expired = 1;
    return t->expired;
}

/* ---- names other code expects -------------------------------------------------------------------
 * SystickInterrupt is the name the start-up code's interrupt table uses for the timer-1 interrupt.
 * GetSysTick1MsCnt / TimeOutSet / IsTimeOut / DelayMs / __udelay are what the vendor USB, efuse and
 * chip-init code call; they stay until our own code replaces those. With all of them (and gSysTick)
 * defined here, the vendor library's own tick object is not linked. */
void SystickInterrupt(void)
{
    gp5_timer_int_clear(1);
    ms_count++;
}

uint32_t GetSysTick1MsCnt(void) { return ms_count; }
void TimeOutSet(void *timer, uint32_t ms) { gp5_timeout_set((gp5_timeout_t *)timer, ms); }

/* The vendor library's IsTimeOut reports an expiry one call late: the call that first sees the deadline
 * passed records it and still returns 0; later calls return 1 (found by the difference test). The
 * vendor USB code was written against that, so this stand-in keeps it. gp5_timeout_expired() does not. */
int gp5_timeout_late(gp5_timeout_t *t)
{
    int was = t->expired;
    if (!was && t->deadline < ms_count) t->expired = 1;
    return was;
}

int IsTimeOut(void *timer) { return gp5_timeout_late((gp5_timeout_t *)timer); }

/* Busy-wait delays the vendor USB and efuse code call. Same loop counts as the vendor library's:
 * from the core clock when it runs from a PLL (clock / 2,000,000 loops per microsecond), otherwise the
 * fixed counts for the internal oscillator; 150 loops are taken off for the call itself. */
/* In the RAM code section, where the vendor library keeps its versions: the loop speed is the delay. */
#define IN_RAM __attribute__((section(".tcm_section"), noinline))

IN_RAM static void spin(uint32_t n)
{
    if ((int32_t)n >= 151) n -= 150;
    if (!n) return;
    __asm__ volatile("1: addi %0, %0, -1\n   bgtz %0, 1b" : "+r"(n));
}

static inline __attribute__((always_inline)) uint32_t core_clock_hz(void)
{
    uint32_t sw = *(volatile uint32_t *)0x40021024u;
    return ((sw & 2u) ? GP5_APLL_HZ : GP5_PLL_HZ) / (*(volatile uint32_t *)0x40021008u + 1u);
}

IN_RAM void gp5_delay_us(uint32_t us)
{
    if (!us) return;
    if (*(volatile uint32_t *)0x40021024u & 1u) spin(((core_clock_hz() & ~1u) / 2000000u) * us);
    else spin(us * 6u);
}

IN_RAM void gp5_delay_ms(uint32_t ms)
{
    if (!ms) return;
    if (*(volatile uint32_t *)0x40021024u & 1u) spin(((core_clock_hz() & ~1u) / 2000u) * ms);
    else spin(ms * 6000u);
}

/* The vendor's names for the same two delays (same code, not a wrapper). */
void __udelay(uint32_t us) __attribute__((alias("gp5_delay_us")));
void DelayMs(uint32_t ms) __attribute__((alias("gp5_delay_ms")));
