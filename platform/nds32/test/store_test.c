/* Host test of gp5_store.c with a simulated NOR flash: programming can only clear bits, erase sets a sector to
 * FF, and the power can be cut in the middle of an erase or a program.
 *   cc -std=c99 -Wall -I platform/nds32 platform/nds32/test/store_test.c -o /tmp/store_test && /tmp/store_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t flash[0x2000];
#define STORE_BYTE(off) (flash[(off)])
#define STORE_WORD(off) (*(const uint32_t *)&flash[(off)])

static int cut_after = -1;           /* power cut: stop after this many bytes of the next program / erase */
static int erases, programs;

int gp5_flash_erase(uint32_t addr);
int gp5_flash_program(uint32_t addr, const uint32_t *src, unsigned len);
#include "../gp5_store.c"

int gp5_flash_erase(uint32_t addr)
{
    unsigned off = addr - GP5_FLASH_STORE, n = GP5_FLASH_SECTOR;
    assert(off % GP5_FLASH_SECTOR == 0 && off < sizeof flash);
    erases++;
    if (cut_after >= 0) { if ((unsigned)cut_after < n) n = (unsigned)cut_after; cut_after = -2; }
    memset(flash + off, 0xFF, n);
    return 0;
}

int gp5_flash_program(uint32_t addr, const uint32_t *src, unsigned len)
{
    unsigned off = addr - GP5_FLASH_STORE, i;
    assert(off + len <= sizeof flash && len % 4 == 0);
    programs++;
    if (cut_after >= 0) { if ((unsigned)cut_after < len) len = (unsigned)cut_after; cut_after = -2; }
    for (i = 0; i < len; i++) flash[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}

#define ID_A 0x41414141u
#define ID_B 0x42424242u

static void fill(uint8_t *d, unsigned n, unsigned seed) { for (unsigned i = 0; i < n; i++) d[i] = (uint8_t)(seed * 31 + i * 7); }

int main(void)
{
    uint8_t in[GP5_STORE_MAX], out[GP5_STORE_MAX];
    unsigned i, k;
    memset(flash, 0xFF, sizeof flash);

    /* nothing saved yet */
    assert(gp5_store_load(ID_A, out, sizeof out) == 0);

    /* round trip, several lengths, newest wins */
    for (k = 1; k <= 300; k++) {
        unsigned n = (k * 37) % 200 + 1;
        fill(in, n, k);
        assert(gp5_store_save(ID_A, in, n) == 0);
        memset(out, 0, sizeof out);
        assert(gp5_store_load(ID_A, out, sizeof out) == n);
        assert(memcmp(in, out, n) == 0);
    }
    printf("300 saves of 1..200 bytes: %d erases, %d programs\n", erases, programs);
    assert(erases < 30);

    /* two firmwares' ids: each finds its own, as long as its last record has not been overwritten */
    fill(in, 100, 7); assert(gp5_store_save(ID_B, in, 100) == 0);
    fill(in, 50, 8); assert(gp5_store_save(ID_A, in, 50) == 0);
    assert(gp5_store_load(ID_A, out, sizeof out) == 50 && memcmp(in, out, 50) == 0);
    fill(in, 100, 7);
    assert(gp5_store_load(ID_B, out, sizeof out) == 100 && memcmp(in, out, 100) == 0);
    assert(gp5_store_load(0x43434343u, out, sizeof out) == 0);
    /* load copies at most max bytes but reports the stored length */
    memset(out, 0xEE, sizeof out);
    assert(gp5_store_load(ID_B, out, 10) == 100 && out[10] == 0xEE);

    /* largest record; too large is refused */
    fill(in, GP5_STORE_MAX, 9);
    assert(gp5_store_save(ID_A, in, GP5_STORE_MAX) == 0);
    assert(gp5_store_load(ID_A, out, sizeof out) == GP5_STORE_MAX && memcmp(in, out, GP5_STORE_MAX) == 0);
    assert(gp5_store_save(ID_A, in, GP5_STORE_MAX + 1) == -1);

    /* power cuts: during a program at every possible point, and during an erase. The previous value
     * survives every cut, and the store keeps working afterwards. */
    for (k = 0; k < 400; k++) {
        uint8_t prev[200], next[200];
        unsigned n = 150;
        fill(prev, n, 1000 + k); assert(gp5_store_save(ID_A, prev, n) == 0);
        fill(next, n, 2000 + k);
        cut_after = (int)((k * 13) % (16 + n));                    /* somewhere inside the record */
        gp5_store_save(ID_A, next, n);                             /* may fail half-way: that is the point */
        i = gp5_store_load(ID_A, out, sizeof out);
        if (cut_after == -2) {                                     /* the cut happened */
            assert(i == n && (memcmp(out, prev, n) == 0 || memcmp(out, next, n) == 0));
        }
        cut_after = -1;
        fill(next, n, 3000 + k); assert(gp5_store_save(ID_A, next, n) == 0);
        assert(gp5_store_load(ID_A, out, sizeof out) == n && memcmp(out, next, n) == 0);
    }
    /* a cut early in an erase: the sector starts blank but holds old bytes further on; saving still works */
    for (k = 0; k < 64; k++) {
        unsigned n = 600;                                          /* big records: a sector switch every 6 saves */
        int before = erases;
        fill(in, n, 5000 + k);
        cut_after = (int)(k * 61 % 4096);
        gp5_store_save(ID_A, in, n);
        if (erases == before) cut_after = -1;                      /* no erase this time: try the next save */
        cut_after = -1;
        fill(in, n, 6000 + k); assert(gp5_store_save(ID_A, in, n) == 0);
        assert(gp5_store_load(ID_A, out, sizeof out) == n && memcmp(out, in, n) == 0);
    }
    puts("ALL OK: round trips, ids, size limits, power cuts during program and erase");
    return 0;
}
