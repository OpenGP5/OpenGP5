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
#define LED_IRQ_LIMIT 64u                 /* expected 20 calls per 1 ms tick */
#define LED_MAIN_TIMEOUT_MS 500u

volatile unsigned gp5_led_status;
static volatile unsigned effect_on;
static volatile uint32_t main_heartbeat;

void led_set_effect(unsigned on) { effect_on = !!on; main_heartbeat++; }

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
    static uint32_t tick;
    static uint32_t last_heartbeat, last_progress;
    static unsigned calls;
    uint32_t now, pins;

    gp5_timer_int_clear(2);
    now = gp5_ms();
    if (now != tick) { tick = now; calls = 0; }
    if (main_heartbeat != last_heartbeat) {
        last_heartbeat = main_heartbeat;
        last_progress = now;
    }
    if (++calls > LED_IRQ_LIMIT || (uint32_t)(now - last_progress) > LED_MAIN_TIMEOUT_MS) {
        /* A bad timer configuration must not monopolise the control processor.
         * Keep the established tick/UART interrupts and the UI running. */
        led_fault();
        return;
    }
    pins = led_pwm_next(&pwm, effect_on);
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
    NVIC_SetPriority(GP5_IRQ_TIMER2, 3);       /* below working tick (0) and UART RX (2) */
    gp5_led_status = 1;
    gp5_timer_start(2);
    NVIC_EnableIRQ(GP5_IRQ_TIMER2);
}
