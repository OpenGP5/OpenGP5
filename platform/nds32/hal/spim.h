#ifndef GP5_HAL_SPIM_H
#define GP5_HAL_SPIM_H
#include <stdint.h>
/* SPI master of the BP1048 (the display's bus), our own driver. Only what the GP-5 uses:
 * port 0 on A5 / A6 / A7, SPI mode 0, the fastest clock setting, transmit only. */
void gp5_spim_pins_port0(void);     /* A5, A6, A7 to the SPI master */
void gp5_spim_clear_int(void);
void gp5_spim_init(void);           /* module reset, mode 0, clock divider 0 */
void gp5_spim_send(const uint8_t *buf, uint32_t n);
#endif
