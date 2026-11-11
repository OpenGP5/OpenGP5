/* Backlight scenarios (timer 5 as a PWM on pin A10): every call the platform makes, one per scenario,
 * with its real arguments, plus duty updates at 0, 37 and 100 %. Builds against the vendor library or
 * (-DOURS) against platform/nds32/hal/backlight.c. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x4002F000u, 0x4002F030u }, { 0x40010000u, 0x40010200u } };
const unsigned n_windows = 2;
#define REG(a) (*(volatile uint32_t *)(a))
#define OUT(v) (REG(0x400101F0u) = (uint32_t)(v))

#ifdef OURS
#include "hal/backlight.h"
#define PIN_GPIO()    (gp5_backlight_pin(0), 0)
#define PIN_PWM()     (gp5_backlight_pin(1), 0)
#define CONFIG()      (gp5_backlight_timer_setup(), 0)
#define ENABLE()      (gp5_backlight_timer_start(), 0)
#define DUTY(pc)      (gp5_backlight_duty(pc), 0)
#else
#include "type.h"
#include "timer.h"
#include "pwm.h"
static int config(void)
{
    PWM_StructInit p;
    __builtin_memset(&p, 0, sizeof p);
    p.CounterMode = PWM_COUNTER_MODE_DOWN;
    p.OutputType  = PWM_OUTPUT_SINGLE_1;
    p.FreqDiv     = 4000;
    p.Duty        = 0;
    p.DMAReqEnable = 0;
    return (int)PWM_Config(TIMER5, &p);
}
static int duty(unsigned pc)
{
    PWM_IOCTRL_ARG a;
    a.OutputType = PWM_OUTPUT_SINGLE_1;
    a.FreqDiv    = 100;
    a.Duty       = (uint16_t)pc;
    return (int)PWM_IOCTRl(TIMER5, OUTPUT_DUTY_UPDATE, &a);
}
#define PIN_GPIO()    (PWM_GpioConfig(TIMER5_PWM_A10_A24_B0, 0, PWM_IO_MODE_NONE), 0)
#define PIN_PWM()     (PWM_GpioConfig(TIMER5_PWM_A10_A24_B0, 0, PWM_IO_MODE_OUT), 0)
#define CONFIG()      config()
#define ENABLE()      ((int)PWM_Enable(TIMER5))
#define DUTY(pc)      duty(pc)
#endif

static void pin_gpio(void) { OUT(PIN_GPIO()); }
static void pin_pwm(void) { OUT(PIN_PWM()); }
static void setup(void) { OUT(CONFIG()); }
static void start(void) { OUT(ENABLE()); }
static void duty100(void) { OUT(DUTY(100)); }
static void duty37(void) { OUT(DUTY(37)); }
static void duty0(void) { OUT(DUTY(0)); }
/* the whole sequence as the platform runs it */
static void all(void) { PIN_GPIO(); PIN_PWM(); CONFIG(); ENABLE(); OUT(DUTY(100)); }

const scenario_t scenarios[] = {
    { "pin_gpio", pin_gpio }, { "pin_pwm", pin_pwm }, { "setup", setup }, { "start", start },
    { "duty100", duty100 }, { "duty37", duty37 }, { "duty0", duty0 }, { "all", all },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
