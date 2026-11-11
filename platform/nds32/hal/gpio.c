#include "gpio.h"

/* Pad set-up registers (port A; each pin has its own field, and the fields differ in width from pin to
 * pin, so this is a table of what was observed, not a formula):
 *   0x400100E8..F4  pin function select        0x400100F8  the same for port B
 *   0x40010094      one drive bit per pin      0x4001008C / 0x40010090  pull-down-current field per pin
 *   0x40010098      pull-down-current fields, port B
 * For "plain digital output as stock has it" every field is cleared, except the drive bit of A22 and
 * A29, which is set. */
#define MUX_A0  0x400100E8u
#define MUX_A1  0x400100ECu
#define MUX_A2  0x400100F0u
#define MUX_A3  0x400100F4u
#define MUX_B   0x400100F8u
#define DRIVE_A 0x40010094u
#define PDC_A0  0x4001008Cu
#define PDC_A1  0x40010090u
#define PDC_B   0x40010098u

static const struct { uint8_t pin, drive_high; uint32_t mux, mux_mask, pdc, pdc_mask; } pad_a[] = {
    {  7, 0, MUX_A0, 0x70000000u, PDC_A0, 0x0000C000u },
    {  9, 0, MUX_A1, 0x00000070u, PDC_A0, 0x000C0000u },
    { 15, 0, MUX_A1, 0x10000000u, PDC_A0, 0xC0000000u },
    { 16, 0, MUX_A2, 0x00000003u, PDC_A1, 0x00000003u },
    { 22, 1, MUX_A2, 0x0F000000u, PDC_A1, 0x00001000u },
    { 24, 0, MUX_A3, 0x0000000Fu, PDC_A1, 0x00030000u },
    { 29, 1, MUX_A3, 0x00F00000u, PDC_A1, 0x04000000u },
    { 30, 0, MUX_A3, 0x07000000u, PDC_A1, 0x30000000u },
    { 31, 0, MUX_A3, 0x70000000u, PDC_A1, 0xC0000000u },
};

int gp5_gpio_a_digital(unsigned pin)
{
    unsigned i;
    for (i = 0; i < sizeof pad_a / sizeof pad_a[0]; i++) {
        if (pad_a[i].pin != pin) continue;
        gp5_reg_bits_clear(GP5_GPIO_A_ANA_EN, GP5_PIN(pin));
        gp5_reg_bits_clear(pad_a[i].mux, pad_a[i].mux_mask);
        if (pad_a[i].drive_high) gp5_reg_bits_set(DRIVE_A, GP5_PIN(pin));
        else gp5_reg_bits_clear(DRIVE_A, GP5_PIN(pin));
        gp5_reg_bits_clear(pad_a[i].pdc, pad_a[i].pdc_mask);
        return 1;
    }
    return 0;
}

int gp5_gpio_b_digital(unsigned pin)
{
    if (pin != 5) return 0;
    gp5_reg_bits_clear(MUX_B, 0x00300000u);
    gp5_reg_bits_clear(PDC_B, 0x00000C00u);
    return 1;
}

void gp5_gpio_uart0_pins(void)
{
    uint32_t v = gp5_reg_get(MUX_A0);
    v = (v & ~0x0000000Du) | 0x00000002u;      /* A0: UART0 receive */
    gp5_reg_set(MUX_A0, v);
    v = (v & ~0x00000030u) | 0x000000C0u;      /* A1: UART0 transmit */
    gp5_reg_set(MUX_A0, v);
}

void gp5_gpio_reset_mask(void) { gp5_reg_set(0x40020000u, 1u); }
