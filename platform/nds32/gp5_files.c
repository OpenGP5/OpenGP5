/* Files in the OpenGP5 storage area (gp5_firmware.h: gp5_file, gp5_file_check, gp5_file_each), read-only on the
 * pedal: the computer writes them (OpenGP5 Launchpad). Format: docs/platform/storage.md.
 *
 * 73 sectors of 4 KB in two runs (A: 0x13F000 .. 0x15FFFF, B: 0x162000 .. 0x189FFF). A file starts with a 64-byte
 * header in its first sector, its data follows at +64 over consecutive sectors of the same run. Of several copies
 * of the same firmware id + name, the one with the highest sequence number counts.
 *
 * The whole storage area belongs to the firmware that is installed (0.5): gp5_storage_claim() runs at start-up,
 * before fw_init(). It keeps a mark (store record "GP5O" = the firmware's fw_name()). Another firmware's mark: the
 * area is wiped, everything that firmware kept is gone, and the mark becomes ours. No mark (a fresh pedal, or a
 * firmware from before 0.5): what is there is kept and the mark written, so updating a firmware keeps its data. */
#include <stdint.h>
#include "gp5_firmware.h"
#include "hal/flash.h"

#define MAGIC 0x46355047u                            /* "GP5F" */
#define HDR 64u
#ifndef FILE_BYTE                                    /* how the area is read: memory-mapped (a host test maps it) */
#define FILE_BYTE(a) (*(const volatile uint8_t *)(a))
#define FILE_WORD(a) (*(const volatile uint32_t *)(a))
#define FILE_PTR(a)  ((const uint8_t *)(a))
#endif

#define CLAIM_ID 0x4F355047u                         /* "GP5O" */

static int blank(uint32_t a)
{
    uint32_t i;
    for (i = 0; i < GP5_FLASH_SECTOR; i += 4) if (FILE_WORD(a + i) != 0xFFFFFFFFu) return 0;
    return 1;
}

static int is_default(const char *s, unsigned n)
{
    static const char D[] = "opengp5";
    unsigned i;
    if (n != sizeof D - 1) return 0;
    for (i = 0; i < n && s[i] == D[i]; i++) {}
    return i == n;
}

void gp5_storage_claim(void)
{
    const char *me = fw_name();
    char mark[32];
    unsigned len = 0, n, i;
    uint32_t a;
    while (me[len] && len < sizeof mark) len++;
    n = gp5_store_load(CLAIM_ID, mark, sizeof mark);
    if (n == len) {
        for (i = 0; i < len && mark[i] == me[i]; i++) {}
        if (i == len) return;                        /* ours */
    }
    /* "opengp5" is the platform's default fw_name(): a firmware that doesn't name itself. As a mark it says nothing
     * about who owns the data (platform 0.5.2 wrote it for such firmwares), so it counts as no mark; and such a
     * firmware doesn't mark the area, so a later build of it that does name itself keeps the data. */
    if (n && !is_default(mark, n))                   /* another firmware's: wipe the area (store included) */
        for (a = GP5_FLASH_AREA; a < GP5_FLASH_AREA + GP5_FLASH_AREA_SIZE; a += GP5_FLASH_SECTOR)
            if (!blank(a)) gp5_flash_erase(a);
    if (!is_default(me, len)) gp5_store_save(CLAIM_ID, me, len);
}

static const struct { uint32_t base, sectors; } RUN[2] = {
    { GP5_FLASH_FILES_A, GP5_FLASH_FILES_A_SIZE / GP5_FLASH_SECTOR },
    { GP5_FLASH_FILES_B, GP5_FLASH_FILES_B_SIZE / GP5_FLASH_SECTOR },
};

static uint32_t crc32_at(uint32_t a, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= FILE_BYTE(a++);
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* a valid header at sector s of run r: its address, else 0 */
static uint32_t header_at(unsigned r, unsigned s)
{
    uint32_t a = RUN[r].base + s * GP5_FLASH_SECTOR, len, n;
    if (FILE_WORD(a) != MAGIC || crc32_at(a, 56) != FILE_WORD(a + 56)) return 0;
    len = FILE_WORD(a + 12); n = FILE_WORD(a + 20) & 0xFFFFu;
    if (!n || s + n > RUN[r].sectors || HDR + len > n * GP5_FLASH_SECTOR) return 0;
    return a;
}

static int name_is(uint32_t a, const char *name)
{
    unsigned i;
    for (i = 0; i < 32; i++) {
        uint8_t c = FILE_BYTE(a + 24 + i);
        if (c != (uint8_t)name[i]) return 0;
        if (!c) return 1;
    }
    return 0;
}

static uint32_t find(uint32_t firm, const char *name)
{
    uint32_t best = 0, seq = 0, a;
    unsigned r, s;
    for (r = 0; r < 2; r++)
        for (s = 0; s < RUN[r].sectors; s++)
            if ((a = header_at(r, s)) && FILE_WORD(a + 8) == firm && name_is(a, name) && (!best || FILE_WORD(a + 4) > seq)) {
                best = a; seq = FILE_WORD(a + 4);
            }
    return best;
}

const uint8_t *gp5_file(uint32_t firm, const char *name, uint32_t *len)
{
    uint32_t a = find(firm, name);
    if (!a) return 0;
    if (len) *len = FILE_WORD(a + 12);
    return FILE_PTR(a + HDR);
}

int gp5_file_check(uint32_t firm, const char *name)
{
    uint32_t a = find(firm, name);
    return a && crc32_at(a + HDR, FILE_WORD(a + 12)) == FILE_WORD(a + 16);
}

unsigned gp5_file_each(uint32_t firm, void (*fn)(const char *name, const uint8_t *data, uint32_t len, void *ctx), void *ctx)
{
    uint32_t a;
    unsigned r, s, n = 0;
    char name[33];
    for (r = 0; r < 2; r++)
        for (s = 0; s < RUN[r].sectors; s++) {
            unsigned i;
            if (!(a = header_at(r, s)) || FILE_WORD(a + 8) != firm) continue;
            for (i = 0; i < 32; i++) name[i] = (char)FILE_BYTE(a + 24 + i);
            name[32] = 0;
            if (find(firm, name) != a) continue;       /* an older copy */
            if (fn) fn(name, FILE_PTR(a + HDR), FILE_WORD(a + 12), ctx);
            n++;
        }
    return n;
}
