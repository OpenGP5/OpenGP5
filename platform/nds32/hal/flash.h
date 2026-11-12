#ifndef GP5_HAL_FLASH_H
#define GP5_HAL_FLASH_H
#include <stdint.h>

/* Writing the control processor's own SPI flash, our own driver (see hal/gpio.h for how these are written;
 * the command sequences follow what the vendor driver was observed to do, docs: platform/README.md).
 *
 * Only the STORE area can be erased or written: two 4 KB sectors at 0x160000, between the end of Valeton's
 * app (0x13E168 on V1.0.6) and Valeton's preset store (0x170000 .. 0x189FFF), in no update section. Every
 * other address is refused, so the factory loader (0x0 .. 0xFFFF), the app and Valeton's data are out of
 * reach. The flash commands run from RAM with interrupts off (the app itself runs from this flash); an
 * erase holds the processor for up to a few hundred ms, a 256-byte page for about 1 ms. */
#define GP5_FLASH_STORE      0x160000u
#define GP5_FLASH_STORE_SIZE 0x2000u
#define GP5_FLASH_SECTOR     0x1000u
#define GP5_FLASH_PAGE       0x100u

uint32_t gp5_flash_id(void);                    /* JEDEC id: maker | type << 8 | capacity << 16 */
uint32_t gp5_flash_status(void);                /* status register 1 (bit 0 = busy, bits 2..6 = protection) */
/* Read up to 64 bytes (a multiple of 4) with the controller's read command instead of through the memory map:
 * a check that the controller takes addresses as expected (the bytes must equal the mapped ones). */
int gp5_flash_read_cmd(uint32_t addr, uint32_t *dst, unsigned len);
/* Erase the 4 KB sector at addr (sector-aligned, inside STORE). 0 = done, -1 = refused. */
int gp5_flash_erase(uint32_t addr);
/* Program len bytes (a multiple of 4, addr 4-aligned, all inside STORE) from a RAM buffer. Erased bits are 1;
 * programming can only clear bits. 0 = done, -1 = refused. */
int gp5_flash_program(uint32_t addr, const uint32_t *src, unsigned len);
#endif
