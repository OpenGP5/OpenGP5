#ifndef GP5_HAL_ADC_H
#define GP5_HAL_ADC_H
#include <stdint.h>
/* SAR ADC of the BP1048 (the volume knob), our own driver. Set-up as the stock firmware has it:
 * clock divider setting 7, reference = analogue supply, single conversions, calibrated once. */
void gp5_adc_init(void);
int32_t gp5_adc_read(unsigned channel);     /* one conversion; channel 9 = pin A28 */
#endif
