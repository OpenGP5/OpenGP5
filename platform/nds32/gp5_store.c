/* Storage that survives power-off, for firmwares (gp5_firmware.h: gp5_store_load / gp5_store_save).
 *
 * The two 4 KB sectors of the flash store (hal/flash.h) hold a log of records, appended one after the other:
 *   [magic u32 'GP5S'][sequence u32][firmware id u32][length u16][CRC-16 u16][data, padded to 4 bytes]
 * Loading takes the record with the highest sequence number for the firmware's id whose CRC is right. Saving
 * appends to the sector of the newest record; when it is full, the other sector is erased, the newest record of
 * every OTHER firmware id is copied to its start (so one firmware's saves never wipe another's settings), and the
 * new record follows. A power cut while appending leaves the previous record valid; one during the switch can
 * lose only records that lived in the sector being erased. A sector is erased only every few dozen saves. */
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
static uint32_t keep[GP5_FLASH_SECTOR / 4];          /* other firmwares' newest records, copied across a sector switch */
#define KEEP_IDS 16

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

/* program one record (id, data) at store offset a with sequence seq; 0 = done and read back */
static int put(uint32_t a, uint32_t seq, uint32_t id, const uint8_t *src, unsigned len)
{
    unsigned i, size = rec_size(len);
    if (!erased(a, size)) return -1;
    buf[0] = MAGIC; buf[1] = seq; buf[2] = id;
    for (i = 0; i < size - HDR; i++) ((uint8_t *)buf)[HDR + i] = i < len ? src[i] : 0xFFu;
    buf[3] = len | ((uint32_t)crc16_at(0, (const uint8_t *)buf + HDR, len) << 16);
    if (gp5_flash_program(GP5_FLASH_STORE + a, buf, size)) return -1;
    for (i = 0; i < size / 4; i++) if (STORE_WORD(a + 4 * i) != buf[i]) return -1;   /* read back */
    return 0;
}

int gp5_store_save(uint32_t id, const void *src, unsigned len)
{
    rec_t newest = { 0, 0, 0, 0 };
    uint32_t top = 0xFFFFFFFFu, seq;
    unsigned end[2], s, size = rec_size(len);
    if (len > GP5_STORE_MAX) return -1;
    scan(0, 0, 1, &newest, &top, &end[0]);
    scan(1, 0, 1, &newest, &top, &end[1]);
    seq = newest.found ? top + 1u : 1u;
    s = newest.found ? (newest.addr >= SECT(1)) : 0;
    if (newest.found && end[s] + size <= GP5_FLASH_SECTOR && erased(SECT(s) + end[s], size))
        return put(SECT(s) + end[s], seq, id, (const uint8_t *)src, len);
    /* Switch sectors: keep the newest record of every other id (in RAM: one of them may live in the sector about
     * to be erased), erase the other sector unless it is blank, write them back, then the new record. Records are
     * kept oldest id first; any that would not leave room for the new record are dropped. */
    {
        uint32_t ids[KEEP_IDS], at[KEEP_IDS], sq[KEEP_IDS], k, n = 0, used = 0, a;
        unsigned ln[KEEP_IDS], t, off, i;
        for (t = 0; t < 2; t++)                          /* every valid record: the newest per id (not ours) */
            for (off = 0; off + HDR <= GP5_FLASH_SECTOR; ) {
                uint32_t r = SECT(t) + off, m = STORE_WORD(r), rid, rseq;
                unsigned rl;
                if (m == 0xFFFFFFFFu) break;
                rl = STORE_WORD(r + 12) & 0xFFFFu;
                if (m != MAGIC || rl > GP5_STORE_MAX || off + rec_size(rl) > GP5_FLASH_SECTOR) break;
                rid = STORE_WORD(r + 8); rseq = STORE_WORD(r + 4);
                if (rid != id && crc16_at(r + HDR, 0, rl) == (STORE_WORD(r + 12) >> 16)) {
                    for (k = 0; k < n && ids[k] != rid; k++) {}
                    if (k == n && n < KEEP_IDS) { ids[n] = rid; sq[n] = rseq; at[n] = r; ln[n] = rl; n++; }
                    else if (k < n && rseq > sq[k]) { sq[k] = rseq; at[k] = r; ln[k] = rl; }
                }
                off += rec_size(rl);
            }
        for (k = 1; k < n; k++)                          /* oldest first, so the newest survive if space runs out */
            for (i = k; i > 0 && sq[i - 1] > sq[i]; i--) {
                uint32_t t1 = ids[i]; ids[i] = ids[i - 1]; ids[i - 1] = t1;
                t1 = sq[i]; sq[i] = sq[i - 1]; sq[i - 1] = t1; t1 = at[i]; at[i] = at[i - 1]; at[i - 1] = t1;
                t1 = ln[i]; ln[i] = ln[i - 1]; ln[i - 1] = (unsigned)t1;
            }
        for (k = 0; k < n; k++) used += rec_size(ln[k]);
        while (n && used + size > GP5_FLASH_SECTOR) {    /* no room for all: drop the oldest */
            used -= rec_size(ln[0]);
            for (k = 0; k + 1 < n; k++) { ids[k] = ids[k + 1]; sq[k] = sq[k + 1]; at[k] = at[k + 1]; ln[k] = ln[k + 1]; }
            n--;
        }
        for (k = 0, a = 0; k < n; k++) {                 /* their data into RAM, one after the other (4-byte steps) */
            for (i = 0; i < ln[k]; i++) ((uint8_t *)keep)[a + i] = STORE_BYTE(at[k] + HDR + i);
            at[k] = a; a += (ln[k] + 3u) & ~3u;
        }
        s = newest.found ? !s : 0;
        if (!erased(SECT(s), GP5_FLASH_SECTOR) && gp5_flash_erase(GP5_FLASH_STORE + SECT(s))) return -1;
        for (k = 0, a = SECT(s); k < n; k++) {
            if (put(a, seq++, ids[k], (const uint8_t *)keep + at[k], ln[k])) return -1;
            a += rec_size(ln[k]);
        }
        return put(a, seq, id, (const uint8_t *)src, len);
    }
}
