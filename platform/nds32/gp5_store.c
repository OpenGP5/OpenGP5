/* Storage that survives power-off, for firmwares (gp5_firmware.h: gp5_store_load / gp5_store_save).
 *
 * The two 4 KB sectors of the flash store (hal/flash.h) hold a log of records, appended one after the other:
 *   [magic u32 'GP5S'][sequence u32][firmware id u32][length u16][CRC-16 u16][data, padded to 4 bytes]
 * Loading takes the record with the highest sequence number for the firmware's id whose CRC is right. Saving
 * appends to the sector of the newest record; when it is full, the other sector is erased and the record goes
 * at its start. A power cut while saving leaves the previous record valid (its sector is never erased while it
 * is the newest), and a sector is erased only every few dozen saves. */
#include <stdint.h>
#include "gp5_firmware.h"
#include "hal/flash.h"

#define MAGIC 0x53355047u                           /* "GP5S" */
#define HDR 16u
#define SECT(i) ((uint32_t)(i) * GP5_FLASH_SECTOR)   /* offsets inside the store */
#ifndef STORE_BYTE                                  /* how the store is read: memory-mapped (a host test maps it elsewhere) */
#define STORE_BYTE(off) (*(const volatile uint8_t *)(GP5_FLASH_STORE + (off)))
#define STORE_WORD(off) (*(const volatile uint32_t *)(GP5_FLASH_STORE + (off)))
#endif

static uint32_t buf[(HDR + GP5_STORE_MAX) / 4];     /* the record being written: programming reads RAM words */

static uint16_t crc16_at(unsigned off, const uint8_t *ram, unsigned n)   /* of the store at off, or of ram */
{
    uint16_t c = 0xFFFFu;
    unsigned i, k;
    for (k = 0; k < n; k++) {
        c ^= (uint16_t)((ram ? ram[k] : STORE_BYTE(off + k)) << 8);
        for (i = 0; i < 8; i++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

static unsigned rec_size(unsigned len) { return HDR + ((len + 3u) & ~3u); }

static int erased(uint32_t off, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i += 4) if (STORE_WORD(off + i) != 0xFFFFFFFFu) return 0;
    return 1;
}

/* Walk one sector: newest valid record for id (or any id when any != 0), and where the next record may go.
 * *end = offset of the first free byte, or GP5_FLASH_SECTOR when the rest cannot be used. */
typedef struct { uint32_t seq, addr; unsigned len; int found; } rec_t;
static void scan(unsigned s, uint32_t id, int any, rec_t *best, uint32_t *top_seq, unsigned *end)
{
    unsigned off = 0;
    while (off + HDR <= GP5_FLASH_SECTOR) {
        uint32_t a = SECT(s) + off, m = STORE_WORD(a);
        unsigned len;
        if (m == 0xFFFFFFFFu) break;                               /* free from here on */
        len = STORE_WORD(a + 12) & 0xFFFFu;
        if (m != MAGIC || len > GP5_STORE_MAX || off + rec_size(len) > GP5_FLASH_SECTOR) { off = GP5_FLASH_SECTOR; break; }
        if (crc16_at(a + HDR, 0, len) == (STORE_WORD(a + 12) >> 16)) {
            uint32_t seq = STORE_WORD(a + 4);
            if (seq > *top_seq || *top_seq == 0xFFFFFFFFu) *top_seq = seq;
            if ((any || STORE_WORD(a + 8) == id) && (!best->found || seq > best->seq)) {
                best->found = 1; best->seq = seq; best->addr = a; best->len = len;
            }
        }
        off += rec_size(len);
    }
    *end = off;
}

unsigned gp5_store_load(uint32_t id, void *dst, unsigned max)
{
    rec_t r = { 0, 0, 0, 0 };
    uint32_t top = 0xFFFFFFFFu;
    unsigned end, i, n;
    scan(0, id, 0, &r, &top, &end);
    scan(1, id, 0, &r, &top, &end);
    if (!r.found) return 0;
    n = r.len < max ? r.len : max;
    for (i = 0; i < n; i++) ((uint8_t *)dst)[i] = STORE_BYTE(r.addr + HDR + i);
    return r.len;
}

int gp5_store_save(uint32_t id, const void *src, unsigned len)
{
    rec_t newest = { 0, 0, 0, 0 };
    uint32_t top = 0xFFFFFFFFu, a;
    unsigned end[2], s, i, size = rec_size(len);
    if (len > GP5_STORE_MAX) return -1;
    scan(0, 0, 1, &newest, &top, &end[0]);
    scan(1, 0, 1, &newest, &top, &end[1]);
    /* after the newest record, if it fits there on erased flash; else the other sector, erased first unless it
     * is entirely erased already (a power cut during an erase can leave old bytes behind a blank start) */
    s = newest.found ? (newest.addr >= SECT(1)) : 0;
    if (!newest.found || end[s] + size > GP5_FLASH_SECTOR || !erased(SECT(s) + end[s], size)) {
        s = newest.found ? !s : 0;
        if (!erased(SECT(s), GP5_FLASH_SECTOR) && gp5_flash_erase(GP5_FLASH_STORE + SECT(s))) return -1;
        end[s] = 0;
    }
    a = SECT(s) + end[s];
    if (!erased(a, size)) return -1;
    buf[0] = MAGIC; buf[1] = newest.found ? top + 1u : 1u; buf[2] = id;
    for (i = 0; i < size - HDR; i++) ((uint8_t *)buf)[HDR + i] = i < len ? ((const uint8_t *)src)[i] : 0xFFu;
    buf[3] = len | ((uint32_t)crc16_at(0, (const uint8_t *)buf + HDR, len) << 16);
    if (gp5_flash_program(GP5_FLASH_STORE + a, buf, size)) return -1;
    for (i = 0; i < size / 4; i++) if (STORE_WORD(a + 4 * i) != buf[i]) return -1;   /* read back */
    return 0;
}
