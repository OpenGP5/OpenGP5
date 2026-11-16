#ifndef GP5_HAL_FLASH_H
#define GP5_HAL_FLASH_H
#include <stdint.h>

/* Writing the control processor's own SPI flash, our own driver (see hal/gpio.h for how these are written;
 * the command sequences follow what the vendor driver was observed to do, docs: platform/README.md).
 *
 * Only the storage area can be erased or written: 0x13F000 .. 0x189FFF, from the end of Valeton's app (0x13E168
 * on V1.0.6) through Valeton's user-preset area, which belongs to whichever firmware is installed
 * (docs/platform/storage.md): FILES run A (0x13F000 .. 0x15FFFF, gp5_files.c), the STORE (two 4 KB sectors at
 * 0x160000, gp5_store.c) and FILES run B (0x162000 .. 0x189FFF). Every other address is refused, so the factory loader (0x0 .. 0xFFFF), the app and Valeton's data are out of reach. The flash commands run from RAM with interrupts off (the app itself runs from this flash); an
 * erase holds the processor for up to a few hundred ms, a 256-byte page for about 1 ms. */
#define GP5_FLASH_STORE      0x160000u
#define GP5_FLASH_STORE_SIZE 0x2000u
#define GP5_FLASH_DATA       0x162000u
#define GP5_FLASH_DATA_SIZE  0xE000u
#define GP5_FLASH_FILES_A    0x13F000u
#define GP5_FLASH_FILES_A_SIZE 0x21000u
#define GP5_FLASH_FILES_B    GP5_FLASH_DATA                            /* 0.5: through Valeton's preset area too */
#define GP5_FLASH_FILES_B_SIZE 0x28000u                                  /* 0x162000 .. 0x189FFF, 40 sectors */
#define GP5_FLASH_AREA       GP5_FLASH_FILES_A                         /* everything writable, A .. B */
#define GP5_FLASH_AREA_SIZE  (GP5_FLASH_FILES_B + GP5_FLASH_FILES_B_SIZE - GP5_FLASH_FILES_A)
#define GP5_FLASH_SECTOR     0x1000u
#define GP5_FLASH_PAGE       0x100u

uint32_t gp5_flash_id(void);                    /* JEDEC id: maker | type << 8 | capacity << 16 */
uint32_t gp5_flash_status(void);                /* status register 1 (bit 0 = busy, bits 2..6 = protection) */
/* Read up to 64 bytes (a multiple of 4) with the controller's read command instead of through the memory map:
 * a check that the controller takes addresses as expected (the bytes must equal the mapped ones). */
int gp5_flash_read_cmd(uint32_t addr, uint32_t *dst, unsigned len);
/* Erase the 4 KB sector at addr (sector-aligned, inside the storage area). 0 = done, -1 = refused. */
int gp5_flash_erase(uint32_t addr);
/* Program len bytes (a multiple of 4, addr 4-aligned, all inside the storage area) from a RAM buffer. Erased bits are 1;
 * programming can only clear bits. 0 = done, -1 = refused. */
int gp5_flash_program(uint32_t addr, const uint32_t *src, unsigned len);
#endif
