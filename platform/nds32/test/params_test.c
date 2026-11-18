/* Host test of gp5_params.c: defaults, clamping, DSP delivery, saving, presets, and a firmware update that
 * reorders / adds / removes parameters (version A writes a store file, version B reads it).
 *   platform/nds32/test/params_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define GP5_CTL_SIDE
#define GP5_HAS_PARAMS
#define GP5_PRESETS 4u
#define GP5_PARAMS_HOST_TEST
uint32_t gp5_ms(void);
const char *fw_name(void) { return "params-test"; }
/* the store: records in memory, kept in a file between the two versions */
#define NREC 64
static struct { uint32_t id; unsigned len; uint8_t d[1024]; } recs[NREC];
static unsigned nrec, saves;
unsigned gp5_store_load(uint32_t id, void *dst, unsigned max)
{
    for (unsigned i = 0; i < nrec; i++) if (recs[i].id == id) { unsigned n = recs[i].len < max ? recs[i].len : max; memcpy(dst, recs[i].d, n); return recs[i].len; }
    return 0;
}
int gp5_store_save(uint32_t id, const void *src, unsigned len)
{
    unsigned i;
    saves++;
    for (i = 0; i < nrec && recs[i].id != id; i++) ;
    if (i == nrec) nrec++;
    recs[i].id = id; recs[i].len = len; memcpy(recs[i].d, src, len);
    return 0;
}
static uint32_t now;
uint32_t gp5_ms(void) { return now; }
static unsigned changed[16], nchanged;
void fw_param_changed(unsigned id) { if (nchanged < 16) changed[nchanged++] = id; }
#include "../gp5_params.c"
void gp5_params_init(void); void gp5_params_poll(void); int gp5_params_take(uint32_t *id, uint32_t *value);
static int fails;
#define CHECK(c, what) do { if (!(c)) { fails++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)
static void tick(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 10) { now += 10; gp5_params_poll(); } }
static unsigned drain(uint32_t *ids) { uint32_t id, v; unsigned n = 0; while (gp5_params_take(&id, &v)) ids[n++] = id; return n; }

int main(int argc, char **argv)
{
    FILE *f;
    uint32_t ids[64];
#ifndef VERSION_B
    if (argc == 3 && !strcmp(argv[1], "A")) {
        gp5_params_init();
        CHECK(gp5_param(GP5_P_MIX) == 50 && gp5_param(GP5_P_TIME) == 350 && gp5_param(GP5_P_BRIGHT) == 7, "defaults");
        CHECK(drain(ids) == 3 && ids[0] == GP5_P_MIX && ids[1] == GP5_P_TIME && ids[2] == GP5_P_SHAPE, "all DSP parameters sent at start");
        CHECK(gp5_param_set(GP5_P_TIME, 5000) && gp5_param(GP5_P_TIME) == 2000, "clamped to max");
        CHECK(!gp5_param_set(GP5_P_TIME, 2000), "the same value is no change");
        CHECK(gp5_param_set(GP5_P_SHAPE, 9) && gp5_param(GP5_P_SHAPE) == 2, "a choice clamped to its last");
        CHECK(drain(ids) == 2, "changed DSP parameters sent once each");
        gp5_param_set(GP5_P_BRIGHT, 3);
        CHECK(drain(ids) == 0, "a control-only parameter is not sent to the DSP");
        tick(1000); CHECK(saves == 0, "not saved within 2 s");
        tick(1100); CHECK(saves == 1, "saved once, 2 s after the last change");
        CHECK(gp5_params_set_outside(GP5_P_STATE, 2) == 0, "read-only refused from outside");
        nchanged = 0; CHECK(gp5_params_set_outside(GP5_P_MIX, 80) == 1 && nchanged == 1 && changed[0] == GP5_P_MIX, "outside change heard");
        /* presets */
        CHECK(gp5_preset_count() == 4 && !gp5_preset_used(0) && gp5_preset_current() == -1, "no presets yet");
        CHECK(gp5_preset_save(1) && gp5_preset_used(1) && gp5_preset_current() == 1 && !gp5_preset_dirty(), "save preset 1");
        CHECK(gp5_preset_rename(1, "Long name here!") && !strcmp(gp5_preset_name(1), "Long name he"), "renamed, cut to 12");
        gp5_param_set(GP5_P_MIX, 10); CHECK(gp5_preset_dirty(), "dirty after a change");
        gp5_param_set(GP5_P_BRIGHT, 9); 
        nchanged = 0;
        CHECK(gp5_preset_load(1) && gp5_param(GP5_P_MIX) == 80 && !gp5_preset_dirty(), "preset 1 loaded back");
        CHECK(gp5_param(GP5_P_BRIGHT) == 9, "a SAVE parameter is not part of presets");
        CHECK(nchanged == 1 && changed[0] == GP5_P_MIX, "a preset load tells the firmware what changed");
        CHECK(!gp5_preset_load(2), "an empty preset doesn't load");
        tick(2100);
        f = fopen(argv[2], "wb"); fwrite(&nrec, sizeof nrec, 1, f); fwrite(recs, sizeof recs[0], nrec, f); fclose(f);
    }
#else
    (void)ids;
    if (argc == 3 && !strcmp(argv[1], "B")) {
        f = fopen(argv[2], "rb"); if (!f || fread(&nrec, sizeof nrec, 1, f) != 1 || fread(recs, sizeof recs[0], nrec, f) != nrec) { puts("FAIL no store file"); return 1; }
        fclose(f);
        gp5_params_init();
        CHECK(gp5_param(GP5_P_MIX) == 80 && gp5_param(GP5_P_TIME) == 2000 && gp5_param(GP5_P_SHAPE) == 2, "kept across a reordered firmware");
        CHECK(gp5_param(GP5_P_BRIGHT) == 9, "a SAVE parameter kept");
        CHECK(gp5_param(GP5_P_DEPTH) == 20, "a new parameter starts at its default");
        CHECK(gp5_preset_current() == 1 && gp5_preset_used(1) && !strcmp(gp5_preset_name(1), "Long name he"), "presets and names kept");
        gp5_param_set(GP5_P_DEPTH, 99); gp5_param_set(GP5_P_MIX, 1);
        CHECK(gp5_preset_load(1) && gp5_param(GP5_P_MIX) == 80 && gp5_param(GP5_P_DEPTH) == 99, "an old preset loads; the new parameter keeps its value");
    }
#endif
    else { puts("params_test A|B store-file"); return 2; }
    printf("%s %s\n", argv[1], fails ? "FAILED" : "ok");
    return fails != 0;
}
