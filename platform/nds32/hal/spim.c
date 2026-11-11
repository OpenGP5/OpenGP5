#include "spim.h"

#define SPIM      0x40003000u
#define W(a)      (*(volatile uint32_t *)(a))
#define H(a)      (*(volatile uint16_t *)(a))
#define B(a)      (*(volatile uint8_t *)(a))
#define CTRL      (SPIM + 0x0C)      /* 16-bit */
#define PIN_MUX_A 0x400100E8u
#define RST0      0x40022000u        /* module reset lines, low = in reset */
#define RST1      0x40022004u

static void field(uint32_t reg, uint32_t mask, uint32_t val) { W(reg) = (W(reg) & ~mask) | val; }

void gp5_spim_pins_port0(void)
{
    field(PIN_MUX_A, 0x70000000u, 0x10000000u);    /* A7 */
    field(PIN_MUX_A, 0x07000000u, 0x04000000u);    /* A6 */
    field(PIN_MUX_A, 0x00700000u, 0x00500000u);    /* A5 */
}

void gp5_spim_clear_int(void) { B(SPIM + 0x10) |= 0x08; }

void gp5_spim_init(void)
{
    W(RST0) &= ~0x80u;       W(RST0) |= 0x80u;           /* pulse the module's two reset lines */
    W(RST1) &= ~(1u << 18);  W(RST1) |= 1u << 18;
    H(CTRL) &= (uint16_t)~0x20u;                         /* SPI mode 0 */
    H(CTRL) &= (uint16_t)~0x10u;
    H(CTRL) = (uint16_t)(H(CTRL) & ~0x0Fu);              /* clock divider 0 */
    H(CTRL) &= (uint16_t)~0x40u;
    H(CTRL) &= (uint16_t)~0x80u;
    H(CTRL) &= (uint16_t)~0x100u;
    H(CTRL) &= (uint16_t)~0x200u;
    H(CTRL) |= 0x400u;
    H(CTRL) &= (uint16_t)~0x800u;
    W(SPIM + 0x10) = 0;
}

void gp5_spim_send(const uint8_t *buf, uint32_t n)
{
    volatile uint32_t i = 0;

    W(SPIM + 0x10) = 0x0F;                               /* clear the status flags */
    W(SPIM + 0x18) = n;                                  /* bytes in this transfer */
    H(CTRL) &= (uint16_t)~0x40u;
    H(CTRL) |= 0x100u;                                   /* transmit */
    H(CTRL) &= (uint16_t)~0x200u;
    W(SPIM + 0x08) = 1;                                  /* start */
    do {
        if (W(SPIM + 0x14) & (1u << 14)) {               /* room for a byte */
            W(SPIM + 0x04) = buf[i];
            i++;
        }
    } while (i < n);
    while (W(SPIM + 0x14) & (1u << 4)) {}                /* until the last byte is out */
    H(CTRL) &= (uint16_t)~0x100u;
}
