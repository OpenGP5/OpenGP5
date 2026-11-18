/* Host test of gp5_log.c: line format, reading in pieces, a ring that wraps.
 *   cc -std=c99 -Wall -I platform/nds32/test/include -I platform/nds32 -I platform/dsp platform/nds32/test/log_test.c */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static uint32_t now;
uint32_t gp5_ms(void) { return now; }
#include "../gp5_log.c"
static int fails;
#define CHECK(c, w) do { if (!(c)) { fails++; printf("FAIL %s (line %d)\n", w, __LINE__); } } while (0)
static unsigned read_all(uint32_t from, char *out, unsigned max, uint32_t *first)
{
    uint8_t b[48]; uint32_t at, end; unsigned n = 0, got; int first_set = 0;
    for (;;) {
        got = gp5_log_read(from, b, sizeof b, &at, &end);
        if (!first_set) { *first = at; first_set = 1; }
        if (n + got > max) got = max - n;
        memcpy(out + n, b, got); n += got; from = at + got;
        if (!got || from >= end) break;
    }
    out[n] = 0; return n;
}
int main(void)
{
    static char all[8192]; uint32_t first;
    now = 12345; gp5_log("hello %d", 42);
    now = 99999; gp5_log("pitch %.1f Hz\n", 440.0);
    gp5_log_dsp((const uint8_t *)"from the DSP", 12);
    read_all(0, all, sizeof all - 1, &first);
    CHECK(!strcmp(all, "[   12.345] hello 42\n[   99.999] pitch 440.0 Hz\n[   99.999] dsp: from the DSP\n"), "three lines, as written");
    for (int i = 0; i < 500; i++) gp5_log("line %03d of many, to wrap the ring around", i);
    read_all(0, all, sizeof all - 1, &first);
    CHECK(first > 0 && strlen(all) == 4096, "from 0 after wrapping: the last 4096 bytes, starting later");
    CHECK(strstr(all, "line 499 of many") && !strstr(all, "line 000 of many"), "the newest kept, the oldest gone");
    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails); return fails != 0;
}
