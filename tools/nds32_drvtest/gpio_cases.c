/* GPIO scenarios: every GPIO driver call the platform makes, with its real arguments. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x40010000u, 0x40010200u }, { 0x40020000u, 0x40024000u } };
const unsigned n_windows = 2;

#ifdef OURS
/* our driver: platform/nds32/hal/gpio.c, no SDK header in sight */
#include "hal/gpio.h"
#define OUT_PIN_A(n)   gp5_gpio_a_digital(n)
#define OUT_PIN_B5()   gp5_gpio_b_digital(5)
#define UART0_PINS()   gp5_gpio_uart0_pins()
#define RESET_MASK()   gp5_gpio_reset_mask()
#define BITS_SET(r, m) gp5_reg_bits_set(GP5_GPIO_##r, m)
#define BITS_CLR(r, m) gp5_reg_bits_clear(GP5_GPIO_##r, m)
#define REG_SET(r, v)  gp5_reg_set(GP5_GPIO_##r, v)
#define REG_GET(r)     gp5_reg_get(GP5_GPIO_##r)
#else
/* the vendor library, called exactly as the platform called it */
#include "type.h"
#include "gpio.h"
extern void GPIO_RegisterResetMask(void);
#define OUT_PIN_A(n)   do { GPIO_PortAModeSet((GPIO_PortA)(1u << (n)), 0); GPIO_PortAOutDsSet((GPIO_INDEX)(1u << (n)), 2); \
                            GPIO_PortAPulldownSet((GPIO_PortA)(1u << (n)), 0); } while (0)
#define OUT_PIN_B5()   do { GPIO_PortBModeSet(GPIOB5, 0); GPIO_PortBOutDsSet(GPIO_INDEX5, 2); GPIO_PortBPulldownSet(GPIOB5, 0); } while (0)
#define UART0_PINS()   do { GPIO_PortAModeSet(GPIOA0, 2); GPIO_PortAModeSet(GPIOA1, 0xc); } while (0)
#define RESET_MASK()   GPIO_RegisterResetMask()
#define BITS_SET(r, m) GPIO_RegBitsSet(GPIO_##r, m)
#define BITS_CLR(r, m) GPIO_RegBitsClear(GPIO_##r, m)
#define REG_SET(r, v)  GPIO_RegSet(GPIO_##r, v)
#define REG_GET(r)     GPIO_RegGet(GPIO_##r)
#endif
#define P(n) (1u << (n))

static void a_gpio_22_24(void) { OUT_PIN_A(24); OUT_PIN_A(22); }
static void a_gpio_15(void) { OUT_PIN_A(15); }
static void a_gpio_lcd(void) { OUT_PIN_A(7); OUT_PIN_A(9); OUT_PIN_A(16); }
static void a_gpio_leds(void) { OUT_PIN_A(29); OUT_PIN_A(30); OUT_PIN_A(31); }
static void b_gpio_5(void) { OUT_PIN_B5(); }
static void uart0_pins(void) { UART0_PINS(); }
static void bits(void)
{
    BITS_SET(A_PU, P(22) | P(24)); BITS_CLR(A_IE, P(22) | P(24));
    BITS_SET(B_OE, P(5)); BITS_CLR(B_OUT, P(5));
    BITS_SET(A_ANA_EN, P(28));
    REG_SET(A_SET, P(30)); REG_SET(A_CLR, P(29) | P(31));
    REG_SET(B_PD, REG_GET(B_IN) ^ REG_GET(A_IN));
}
static void reset_mask(void) { RESET_MASK(); }

const scenario_t scenarios[] = {
    { "a_gpio_22_24", a_gpio_22_24 }, { "a_gpio_15", a_gpio_15 }, { "a_gpio_lcd", a_gpio_lcd }, { "a_gpio_leds", a_gpio_leds },
    { "b_gpio_5", b_gpio_5 }, { "uart0_pins", uart0_pins }, { "bits", bits }, { "reset_mask", reset_mask },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
