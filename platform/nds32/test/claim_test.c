/* Host test of gp5_storage_claim() (gp5_files.c): who owns the storage area at start-up.
 *   cc -std=c99 -Wall -I platform/nds32 platform/nds32/test/claim_test.c -o /tmp/claim_test && /tmp/claim_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "gp5_firmware.h"
#include "hal/flash.h"

static uint8_t area[GP5_FLASH_AREA_SIZE];
#define FILE_BYTE(a) (area[(a) - GP5_FLASH_AREA])
#define FILE_WORD(a) (*(const uint32_t *)&area[(a) - GP5_FLASH_AREA])
#define FILE_PTR(a)  (&area[(a) - GP5_FLASH_AREA])

static char mark[33]; static unsigned mark_n;     /* the store's "GP5O" record (0 = none) */
static const char *name;                          /* this firmware's fw_name() */
static int erases;

const char *fw_name(void) { return name; }
unsigned gp5_store_load(uint32_t id, void *dst, unsigned max) { (void)id; unsigned n = mark_n < max ? mark_n : max; memcpy(dst, mark, n); return n; }
int gp5_store_save(uint32_t id, const void *src, unsigned len) { (void)id; memcpy(mark, src, len); mark_n = len; return 0; }
int gp5_flash_erase(uint32_t a) { memset(&area[a - GP5_FLASH_AREA], 0xFF, GP5_FLASH_SECTOR); erases++; mark_n = 0; return 0; }
#include "../gp5_files.c"

static void run(const char *stored, const char *me, int wiped, const char *after)
{
    memset(area, 0xFF, sizeof area); area[0] = 0x12;               /* some data */
    mark_n = stored ? (unsigned)strlen(stored) : 0; if (stored) memcpy(mark, stored, mark_n);
    name = me; erases = 0;
    gp5_storage_claim();
    assert((erases > 0) == wiped);
    assert(after ? mark_n == strlen(after) && !memcmp(mark, after, mark_n) : mark_n == 0);
    printf("ok  mark %-14s firmware %-14s -> %s, mark %s\n", stored ? stored : "(none)", me, wiped ? "wiped" : "kept", after ? after : "(none)");
}

int main(void)
{
    run(NULL, "dreamcatcher", 0, "dreamcatcher");          /* no mark: keep, mark */
    run("dreamcatcher", "dreamcatcher", 0, "dreamcatcher"); /* ours */
    run("nam", "dreamcatcher", 1, "dreamcatcher");          /* another firmware's: wipe */
    run("opengp5", "dreamcatcher", 0, "dreamcatcher");      /* the default name (0.5.2 wrote it): no mark */
    run(NULL, "opengp5", 0, NULL);                          /* an unnamed firmware doesn't mark */
    run("opengp5", "opengp5", 0, "opengp5");                /* (an old default mark stays until a named firmware) */
    run("nam", "opengp5", 1, NULL);                         /* an unnamed firmware still clears another's data */
    puts("all ok");
    return 0;
}
