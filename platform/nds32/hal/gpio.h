#ifndef GP5_HAL_GPIO_H
#define GP5_HAL_GPIO_H
#include <stdint.h>

/* GPIO of the GP-5's control processor (BP1048), our own driver: no vendor SDK code or headers.
 * Written from observed register behaviour (tools/nds32_drvtest) and
 * checked by difference against the vendor library in the instruction simulator.
 * NOT run on hardware yet. Only the pins and functions the GP-5 platform uses are covered. */

/* registers, one bit per pin; port A has 32 pins, port B 8 */
#define GP5_GPIO_A_IN      0x40010000u
#define GP5_GPIO_A_OUT     0x40010004u
#define GP5_GPIO_A_SET     0x40010008u   /* write 1 = drive high, no read-modify-write needed */
#define GP5_GPIO_A_CLR     0x4001000Cu   /* write 1 = drive low */
#define GP5_GPIO_A_IE      0x40010014u   /* input enable */
#define GP5_GPIO_A_OE      0x40010018u   /* output enable */
#define GP5_GPIO_A_ANA_EN  0x4001001Cu   /* analogue function */
#define GP5_GPIO_A_PU      0x40010020u
#define GP5_GPIO_A_PD      0x40010024u
#define GP5_GPIO_B_IN      0x40010030u
#define GP5_GPIO_B_OUT     0x40010034u
#define GP5_GPIO_B_IE      0x40010044u
#define GP5_GPIO_B_OE      0x40010048u
#define GP5_GPIO_B_PU      0x40010050u
#define GP5_GPIO_B_PD      0x40010054u

#define GP5_PIN(n) (1u << (n))

static inline uint32_t gp5_reg_get(uint32_t reg) { return *(volatile uint32_t *)reg; }
static inline void gp5_reg_set(uint32_t reg, uint32_t v) { *(volatile uint32_t *)reg = v; }
static inline void gp5_reg_bits_set(uint32_t reg, uint32_t m) { *(volatile uint32_t *)reg |= m; }
static inline void gp5_reg_bits_clear(uint32_t reg, uint32_t m) { *(volatile uint32_t *)reg &= ~m; }

/* Make a pin a plain digital pad with the pad settings the stock firmware uses for its outputs:
 * pin function = GPIO, analogue off, the pad's drive and pull-down-current fields as stock.
 * Direction, level and pull-ups are set afterwards with the registers above.
 * Port A: pins 7, 9, 15, 16, 22, 24, 29, 30, 31. Port B: pin 5. Other pins: nothing happens, returns 0. */
int gp5_gpio_a_digital(unsigned pin);
int gp5_gpio_b_digital(unsigned pin);

/* A0 = UART0 receive, A1 = UART0 transmit (the DSP link) */
void gp5_gpio_uart0_pins(void);

/* written once at start-up by the stock firmware and the vendor library (0x40020000 = 1) */
void gp5_gpio_reset_mask(void);
#endif
