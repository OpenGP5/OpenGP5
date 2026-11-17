#include "led.h"
#include "hal/gpio.h"
#include "hal/tick.h"
#include "start/irq.h"
#include "hal/timer.h"
#include <nds32_intrinsic.h>

/* Same TIMER2 register used by the SDK timer driver. Reset this owned timer's
 * control state, rather than inheriting interrupt/DMA enables from the loader. */
#ifndef TIMER2_CTRL
#define TIMER2_CTRL (*(volatile uint32_t *)0x4002b800)
#endif
#define LED_IRQ_LIMIT 20000u              /* calls with no main-loop progress in between (20 per ms: 1 s). Was 64 calls
                                           * within one ms tick, but the tick itself stops while a higher-priority
                                           * handler runs long (the audio link waiting for its gap after a flash erase,
                                           * 2026-11-17): that latched red and switched the LEDs off for good. A timer
                                           * that really takes the processor over also stops the main loop. */
#define LED_MAIN_TIMEOUT_MS 2000u               /* a stalled main loop shows red while it lasts (loading a NAM capture plus
                                                   * a flash write took > 500 ms at 320 MHz, 2026-11-17: red until a restart) */

volatile unsigned gp5_led_status;
static volatile uint32_t effect_on;       /* fw_active(): 0, non-zero, or a GP5_LED colour */
static volatile uint32_t main_heartbeat;

void led_set_effect(uint32_t on) { effect_on = on; main_heartbeat++; }

static void led_fault(void)
{
    NVIC_DisableIRQ(GP5_IRQ_TIMER2);
    gp5_timer_pause(2, 1);
    gp5_led_status = 2;
    gp5_reg_set(GP5_GPIO_A_CLR, LED_PINS);
    gp5_reg_set(GP5_GPIO_A_SET, LED_R);
}

void Timer2Interrupt(void)
{
    static led_pwm_t pwm;
    static uint32_t last_heartbeat, last_progress;
    static unsigned calls;
    uint32_t now, pins;

    gp5_timer_int_clear(2);
    now = gp5_ms();
    if (main_heartbeat != last_heartbeat) {
        last_heartbeat = main_heartbeat;
        last_progress = now;
        calls = 0;
    }
    if (++calls > LED_IRQ_LIMIT) {
        /* A bad timer configuration must not monopolise the control processor.
         * Keep the established tick/UART interrupts and the UI running. */
        led_fault();
        return;
    }
    /* the main loop has stalled: red while it lasts, its own colour again once it runs */
    pins = (uint32_t)(now - last_progress) > LED_MAIN_TIMEOUT_MS ? LED_R : led_pwm_next(&pwm, effect_on);
    /* Hardware write aliases: no A_OUT read and no global interrupt locking. */
    gp5_reg_set(GP5_GPIO_A_CLR, LED_PINS & ~pins);
    gp5_reg_set(GP5_GPIO_A_SET, pins);
}

void led_start(void)
{
    if (gp5_led_status) return;
    NVIC_DisableIRQ(GP5_IRQ_TIMER2);
    TIMER2_CTRL = 0;
    if (gp5_timer_config(2, LED_PWM_TICK_US, 0) != 0) {
        gp5_led_status = 2;
        return;
    }
    gp5_timer_int_clear(2);
    __nds32__mtsr(1u << GP5_IRQ_TIMER2, NDS32_SR_INT_PEND2);
    /* Above the audio link (3): a firmware's on_block() can run for most of each block (NAM: ~1 ms of 1.45 ms),
     * and while it ran at the same priority the brightness pulses froze (dim colours flickered, 2026-11-15).
     * Level with UART RX (2) and below the tick (0); the handler takes a few microseconds. */
    NVIC_SetPriority(GP5_IRQ_TIMER2, 2);
    gp5_led_status = 1;
    gp5_timer_start(2);
    NVIC_EnableIRQ(GP5_IRQ_TIMER2);
}
