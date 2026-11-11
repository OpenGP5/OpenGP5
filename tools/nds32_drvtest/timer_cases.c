/* Timer and tick scenarios: every timer call the platform makes (1 ms tick on timer 1, 50 us LED timer
 * on timer 2), the tick interrupt handler and the timeout helpers. Builds against the vendor library
 * or (-DOURS) against platform/nds32/hal. Values a call returns are stored in an unused GPIO word.
 * The clock switch is forced to "core clock from a PLL" and the two clock dividers kept small, as on
 * the pedal; which PLL and whether the system divider is bypassed stay random. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x4002B000u, 0x4002B030u }, { 0x4002B800u, 0x4002B830u }, { 0x40021000u, 0x40021040u } };
const unsigned n_windows = 3;
#define REG(a) (*(volatile uint32_t *)(a))
#define OUT(v) (REG(0x400101F0u) = (uint32_t)(v))

#ifdef OURS
#include "hal/timer.h"
#include "hal/tick.h"
#define CLOCKS()        ((void)0)
#define CONFIG(n, us)   gp5_timer_config(n, us, 0)
#define START(n)        (gp5_timer_start(n), 0)
#define PAUSE(n, p)     (gp5_timer_pause(n, p), 0)
#define CLEAR(n)        (gp5_timer_int_clear(n), 0)
#define TICK_ISR()      SystickInterrupt()
#define NOW()           gp5_ms()
/* the timeout helpers are compared under the names the vendor USB library calls (hal/tick.c) */
typedef gp5_timeout_t tmo_t;
void SystickInterrupt(void);
void TimeOutSet(void *timer, uint32_t ms);
int IsTimeOut(void *timer);
#define TMO_SET(t, ms)  TimeOutSet(t, ms)
#define TMO_OUT(t)      IsTimeOut(t)
#else
#include "type.h"
#include "timer.h"
#include "timeout.h"
extern uint32_t gAPLLWorkFreq, gPLLWorkFreq;
extern void SystickInterrupt(void);
#define CLOCKS()        do { gAPLLWorkFreq = 240000000u; gPLLWorkFreq = 288000000u; } while (0)
#define CONFIG(n, us)   Timer_Config((TIMER_INDEX)((n) - 1), us, 0)
#define START(n)        Timer_Start((TIMER_INDEX)((n) - 1))
#define PAUSE(n, p)     Timer_Pause((TIMER_INDEX)((n) - 1), p)
#define CLEAR(n)        Timer_InterruptFlagClear((TIMER_INDEX)((n) - 1), UPDATE_INTERRUPT_SRC)
#define TICK_ISR()      SystickInterrupt()
#define NOW()           GetSysTick1MsCnt()
typedef TIMER tmo_t;
#define TMO_SET(t, ms)  TimeOutSet(t, ms)
#define TMO_OUT(t)      IsTimeOut(t)
#endif

static void clocks(void)
{
    CLOCKS();
    REG(0x40021024u) |= 1u;
    REG(0x40021008u) &= 3u;
    REG(0x4002100Cu) &= 3u;
}
static void t1_config(void) { clocks(); OUT(CONFIG(1, 1000)); }
static void t2_config(void) { clocks(); OUT(CONFIG(2, 50)); }
static void t1_start(void) { OUT(START(1)); }
static void t2_start(void) { OUT(START(2)); }
static void t2_pause(void) { OUT(PAUSE(2, 1)); }
static void t2_resume(void) { OUT(PAUSE(2, 0)); }
static void t1_clear(void) { OUT(CLEAR(1)); }
static void t2_clear(void) { OUT(CLEAR(2)); }
static void tick_isr(void) { uint32_t a = NOW(); TICK_ISR(); TICK_ISR(); TICK_ISR(); OUT(NOW() - a); }
static void timeouts(void)
{
    tmo_t t;
    uint32_t r = 0;
    int i;
    TMO_SET(&t, 5);
    r |= (uint32_t)(TMO_OUT(&t) != 0);                       /* at once: not expired */
    for (i = 0; i < 5; i++) TICK_ISR();
    r |= (uint32_t)(TMO_OUT(&t) != 0) << 1;                  /* after exactly 5 ms */
    TICK_ISR();
    r |= (uint32_t)(TMO_OUT(&t) != 0) << 2;                  /* after 6 ms */
    TMO_SET(&t, 0);
    r |= (uint32_t)(TMO_OUT(&t) != 0) << 3;
    TICK_ISR();
    r |= (uint32_t)(TMO_OUT(&t) != 0) << 4;
    r |= (uint32_t)(TMO_OUT(&t) != 0) << 5;                  /* stays expired */
    OUT(r | 0x100u);
}
const scenario_t scenarios[] = {
    { "t1_config", t1_config }, { "t2_config", t2_config }, { "t1_start", t1_start }, { "t2_start", t2_start },
    { "t2_pause", t2_pause }, { "t2_resume", t2_resume }, { "t1_clear", t1_clear }, { "t2_clear", t2_clear },
    { "tick_isr", tick_isr }, { "timeouts", timeouts },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
