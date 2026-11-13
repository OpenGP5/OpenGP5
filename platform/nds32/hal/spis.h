#ifndef GP5_HAL_SPIS_H
#define GP5_HAL_SPIS_H
#include <stdint.h>
/* SPI slave of the BP1048 with its two DMA channels: the audio stream to and from the DSP, which is
 * the SPI master and starts one transfer per audio block. Our own driver; only what the GP-5 uses:
 * port 1 on A20..A23, clock mode 0, receive on DMA channel 4, send on DMA channel 5, block transfers. */
#define GP5_IRQ_SPIS_DMA 13                                  /* DMA channel 4 (receive) done */
void gp5_spis_init(void);                                    /* pins, slave, both DMA channels, flags cleared */
void gp5_spis_start(const void *tx, void *rx, uint32_t n);   /* arm one exchange of n bytes each way */
int  gp5_spis_done(void);                                    /* both channels finished: 1 (and the slave's DMA requests off) */
void gp5_spis_release(void);                 /* stop; pins A20..A23 back as they were */
void gp5_spis_rearm(const void *tx, void *rx, uint32_t n);   /* clear the flags and arm the next exchange */
#endif
