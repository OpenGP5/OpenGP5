#include "adc.h"

#define ADC   0x40028000u
#define W(a)  (*(volatile uint32_t *)(a))
#define H(a)  (*(volatile uint16_t *)(a))
#define B(a)  (*(volatile uint8_t *)(a))

static void convert(void)
{
    B(ADC + 0x0C) |= 0x02;                   /* clear "done" */
    W(ADC + 0x00) |= 0x01;                   /* start */
    while (!(W(ADC + 0x0C) & 1u)) {}         /* done */
}

void gp5_adc_init(void)
{
    H(ADC + 0x1C) |= 0x0001;                 /* power */
    W(ADC + 0x00) = (W(ADC + 0x00) & ~0x3Cu) | (7u << 2);    /* clock divider setting 7 */
    H(ADC + 0x1C) &= (uint16_t)~0x0002u;     /* reference: analogue supply */
    H(ADC + 0x1C) &= (uint16_t)~0x0200u;
    H(ADC + 0x1C) |= 0x0100;
    W(ADC + 0x00) &= ~0x80u;                 /* single conversions */
    W(ADC + 0x00) |= 0x40u;                  /* calibration run */
    W(ADC + 0x00) &= ~0x80u;
    convert();
    B(ADC + 0x0C) |= 0x02;
    W(ADC + 0x00) &= ~0x40u;
}

int32_t gp5_adc_read(unsigned channel)
{
    W(ADC + 0x08) = (W(ADC + 0x08) & ~0x0Fu) | (channel & 0x0Fu);
    convert();
    return (int16_t)W(ADC + 0x04);
}
