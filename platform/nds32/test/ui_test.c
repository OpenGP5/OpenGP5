/* Host test of the UI kit (gp5_ui.c): value text for every unit, the settings page's picking, adjusting, steps,
 * choices, read-only rows, the presets row, messages, drawing only what changed, and knob pickup.
 *   platform/nds32/test/ui_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define GP5_CTL_SIDE
#define GP5_HAS_PARAMS
#define GP5_PRESETS 4u
#define GP5_PARAMS_HOST_TEST
#include "gp5_firmware.h"
static uint32_t ms; uint32_t gp5_ms(void) { return ms; }
static unsigned knob; unsigned gp5_knob(void) { return knob; }
const char *fw_name(void) { return "ui-test"; }
static struct { uint32_t id; unsigned len; uint8_t d[1024]; } recs[64]; static unsigned nrec;
unsigned gp5_store_load(uint32_t id, void *d, unsigned m) { for (unsigned i = 0; i < nrec; i++) if (recs[i].id == id) { memcpy(d, recs[i].d, recs[i].len < m ? recs[i].len : m); return recs[i].len; } return 0; }
int gp5_store_save(uint32_t id, const void *s, unsigned l) { unsigned i; for (i = 0; i < nrec && recs[i].id != id; i++) {} if (i == nrec) nrec++; recs[i].id = id; recs[i].len = l; memcpy(recs[i].d, s, l); return 0; }
static unsigned blits; static unsigned last_y;
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px) { (void)x; (void)w; (void)h; (void)px; blits++; last_y = y; }
void fw_param_changed(unsigned id) { (void)id; }
int fw_param_text(unsigned id, int32_t v, char *t) { if (id != 7) return 0; sprintf(t, "%d.%02d Hz", (int)v / 100, (int)v % 100); return 1; }
#include "gp5_params.c"
#include "gp5_ui.c"
#include "../fonts/sans_16.h"
void gp5_params_init(void);

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void fmt(unsigned id, int32_t v, const char *want)
{
    char o[16]; gp5_param_format(id, v, o);
    CHECK(!strcmp(o, want), "format %u %d: '%s', want '%s'", id, (int)v, o, want);
}
static int n_saves, n_resets; static char slot_txt[8] = "P1";
void t_save(void) { n_saves++; }
void t_reset(void) { n_resets++; }
const char *t_slot(void) { return slot_txt; }
static int t_saves(void) { return n_saves; }
static int t_resets(void) { return n_resets; }
static void t_set_slot(const char *s) { strcpy(slot_txt, s); }
static unsigned drawn(gp5_ui_page_t *p, int force) { unsigned b = blits; while (gp5_ui_page_draw(p, force)) force = 0; return blits - b; }

int main(void)
{
    gp5_params_init();
    /* value text, as Launchpad shows it */
    fmt(GP5_P_MIX, 50, "50 %"); fmt(GP5_P_TIME, 350, "350 ms"); fmt(GP5_P_TIME, 1500, "1.50 s"); fmt(GP5_P_TIME, 2000, "2 s");
    fmt(GP5_P_SHAPE, 2, "Ramp"); fmt(GP5_P_SHAPE, 0, "Sine");
    fmt(GP5_P_GAIN, -60, "-6 dB"); fmt(GP5_P_GAIN, 55, "+5.5 dB"); fmt(GP5_P_GAIN, 0, "0 dB");
    fmt(GP5_P_PITCH, 7, "+7 st"); fmt(GP5_P_PITCH, -12, "-12 st"); fmt(GP5_P_FINE, -15, "-1.5 cents");
    fmt(GP5_P_FREQ, 440, "440 Hz"); fmt(GP5_P_RATE, 503, "5.03 Hz"); fmt(GP5_P_STATE, 3, "3");

    /* every visible parameter + presets, 4 rows of 20 px */
    static gp5_ui_page_t p = { .font = &gp5_font_sans_16, .row_h = 20 };
    CHECK(drawn(&p, 1) == 4, "first draw: 4 rows");
    CHECK(p.n == 10, "rows: 9 visible parameters + presets, got %u", p.n);
    CHECK(drawn(&p, 0) == 0, "nothing changed: nothing drawn");
    gp5_ui_page_turn(&p, 1);                                  /* pick TIME */
    CHECK(gp5_ui_page_row(&p) == GP5_P_TIME, "picked TIME");
    CHECK(drawn(&p, 0) == 2, "picking redraws the two rows");
    gp5_ui_page_click(&p); gp5_ui_page_turn(&p, 1);          /* TIME: range 1990 -> step 20 (1/50: 39.8) */
    CHECK(gp5_param(GP5_P_TIME) == 370, "TIME step: %d", (int)gp5_param(GP5_P_TIME));
    gp5_ui_page_click(&p);
    gp5_ui_page_turn(&p, -1); gp5_ui_page_click(&p); gp5_ui_page_turn(&p, -3);   /* MIX: step 2 */
    CHECK(gp5_param(GP5_P_MIX) == 44, "MIX step: %d", (int)gp5_param(GP5_P_MIX));
    gp5_ui_page_turn(&p, -100);
    CHECK(gp5_param(GP5_P_MIX) == 0, "clamped at min");
    gp5_ui_page_click(&p); gp5_ui_page_turn(&p, 2); gp5_ui_page_click(&p); gp5_ui_page_turn(&p, -1);   /* SHAPE wraps */
    CHECK(gp5_param(GP5_P_SHAPE) == 2, "choice wraps: %d", (int)gp5_param(GP5_P_SHAPE));
    gp5_ui_page_click(&p);
    gp5_ui_page_turn(&p, 5);                                  /* RATE 503: on step 20's grid from 50 */
    gp5_ui_page_click(&p); gp5_ui_page_turn(&p, 1);
    CHECK(gp5_param(GP5_P_RATE) == 510, "snaps up onto the grid: %d", (int)gp5_param(GP5_P_RATE));
    gp5_ui_page_turn(&p, -1);
    CHECK(gp5_param(GP5_P_RATE) == 490, "then steps: %d", (int)gp5_param(GP5_P_RATE));
    gp5_ui_page_click(&p);
    drawn(&p, 0); CHECK(p.top == 4, "scrolled: top %u", p.top);
    gp5_ui_page_turn(&p, 1); gp5_ui_page_click(&p);         /* STATE is read-only */
    CHECK(!p.adjusting, "read-only rows can't be adjusted");
    gp5_ui_page_turn(&p, 10);                                /* to the end: presets */
    CHECK(gp5_ui_page_row(&p) == GP5_UI_PRESETS && p.cursor == 9, "end: presets row (cursor %u)", p.cursor);
    gp5_ui_page_click(&p); gp5_ui_page_turn(&p, 2);
    CHECK(gp5_ui_page_preset(&p) == 2 && gp5_preset_current() == -1, "an empty preset shown, not loaded");
    CHECK(gp5_preset_save(2), "save");
    gp5_param_set(GP5_P_MIX, 80);
    gp5_ui_page_turn(&p, 1); gp5_ui_page_turn(&p, -1);
    CHECK(gp5_param(GP5_P_MIX) == 0 && gp5_preset_current() == 2, "turning onto a preset loads it");
    /* the message replaces the last row, then goes */
    drawn(&p, 0);
    gp5_ui_page_message(&p, "Saved P3", 1500);
    CHECK(drawn(&p, 0) == 1 && last_y == 60, "message in the last row");
    ms += 1499; CHECK(drawn(&p, 0) == 0, "still there");
    ms += 1; CHECK(drawn(&p, 0) == 1, "gone after 1.5 s");
    p.faded = 1; CHECK(drawn(&p, 0) == 4, "fading redraws the page");

    /* knob pickup: MIX is 0; the knob at 128 (50) doesn't jump it, until it passes the value */
    gp5_param_set(GP5_P_MIX, 30);
    knob = 128; { static gp5_pickup_t k; gp5_pickup_start(&k, GP5_P_MIX);
    knob = 140; gp5_pickup_poll(&k); CHECK(gp5_param(GP5_P_MIX) == 30, "no jump: %d", (int)gp5_param(GP5_P_MIX));
    knob = 60; gp5_pickup_poll(&k); CHECK(gp5_param(GP5_P_MIX) == 24, "picked up after passing: %d", (int)gp5_param(GP5_P_MIX));
    knob = 61; CHECK(!gp5_pickup_poll(&k), "a one-step wobble is ignored");
    knob = 255; gp5_pickup_poll(&k); CHECK(gp5_param(GP5_P_MIX) == 100, "whole range: %d", (int)gp5_param(GP5_P_MIX)); }

    /* the menu: actions, a confirmation, a value on the right, a message */
    {
        static const gp5_ui_item_t items[] = { { "Save", t_save, t_slot, 0 }, { "Reset all", t_reset, 0, 1 }, { "Info", 0, t_slot, 0 },
                                               { "A", t_save, 0, 0 }, { "B", t_save, 0, 0 } };
        static gp5_ui_menu_t m = { .items = items, .n = 5, .font = &gp5_font_sans_16, .row_h = 20, .title = "MENU" };
        unsigned b = blits; { int f = 1; while (gp5_ui_menu_draw(&m, f)) f = 0; }
        CHECK(blits - b == 4, "first draw: title + 3 rows, got %u", blits - b);
        CHECK(gp5_ui_menu_click(&m) == 1 && t_saves() == 1, "a click runs the item picked");
        gp5_ui_menu_turn(&m, 1); while (gp5_ui_menu_draw(&m, 0)) {}
        CHECK(gp5_ui_menu_click(&m) == 0 && t_resets() == 0, "a confirm item asks first");
        b = blits; while (gp5_ui_menu_draw(&m, 0)) {}
        CHECK(blits - b == 1 && m.armed, "the row says click again");
        CHECK(gp5_ui_menu_click(&m) == 1 && t_resets() == 1, "the second click runs it");
        gp5_ui_menu_click(&m); gp5_ui_menu_turn(&m, 1); gp5_ui_menu_turn(&m, -1);
        CHECK(gp5_ui_menu_click(&m) == 0 && t_resets() == 1, "turning away cancels the confirmation");
        ms += 3000; while (gp5_ui_menu_draw(&m, 0)) {}
        CHECK(!m.armed && gp5_ui_menu_click(&m) == 0 && t_resets() == 1, "a confirmation runs out after 3 s");
        gp5_ui_menu_turn(&m, 1);
        CHECK(gp5_ui_menu_click(&m) == 0, "an item without an action does nothing");
        gp5_ui_menu_turn(&m, 10);
        CHECK(gp5_ui_menu_item(&m) == 4, "clamped at the last item");
        b = blits; while (gp5_ui_menu_draw(&m, 0)) {}
        CHECK(m.top == 2, "scrolled to keep it in view: top %u", m.top);
        gp5_ui_menu_pick(&m, 0); while (gp5_ui_menu_draw(&m, 0)) {}
        t_set_slot("P3"); b = blits; while (gp5_ui_menu_draw(&m, 0)) {}
        CHECK(blits - b == 2, "a value that changes redraws the two rows that show it only (%u)", blits - b);
        gp5_ui_menu_message(&m, "Saved", 1500);
        b = blits; while (gp5_ui_menu_draw(&m, 0)) {}
        CHECK(blits - b == 1 && last_y == 60, "the message in the last row");
    }

    printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails != 0;
}
