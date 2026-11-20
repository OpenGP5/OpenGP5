/* OpenGP5 UI kit (1.0, control processor; docs/platform/ui.md): the pieces most firmwares' screens and controls need,
 * built on parameters (gp5_params.h). All optional: a firmware with its own look (Dreamcatcher) uses none or some.
 *
 *   gp5_param_format()   a value as text, the way Launchpad shows it ("50 %", "-6 dB", "1.50 s", a choice's name)
 *   gp5_ui_page_t        a settings page: one row per parameter (and optionally the presets), PARA turns to pick a
 *                        row, a click starts / ends adjusting it, turning then changes the value
 *   gp5_ui_page_message  a short message in the page's last row ("Saved P2"), for a time
 *   gp5_ui_menu_t        a menu of actions: PARA picks, a click runs one, risky ones ask for a second click
 *   gp5_pickup_t         the volume knob takes over a parameter without a jump: it acts once it passes the value
 *
 * A page with four parameters and the presets, the tremolo's:
 *
 *     static const uint8_t rows[] = { GP5_P_RATE, GP5_P_DEPTH, GP5_P_SHAPE, GP5_UI_PRESETS };
 *     static gp5_ui_page_t page = { .rows = rows, .n = 4, .font = &gp5_font_sans_16, .row_h = 20, .upper = 1 };
 *     void fw_para_turn(int d) { gp5_ui_page_turn(&page, d); }
 *     void fw_para_click(void) { gp5_ui_page_click(&page); }
 *     int fw_draw(int force)   { return gp5_ui_page_draw(&page, force); }
 *
 * Built only for a firmware with params.h (GP5_HAS_PARAMS). */
#ifndef GP5_UI_H
#define GP5_UI_H
#include <stdint.h>
#include "gp5_params.h"
#include "gp5_font.h"

/* value -> text (up to 15 characters and a NUL; out needs 16): choices by name, then the unit; GP5_U_CUSTOM asks
 * the firmware's fw_param_text() (the number if it has none). Returns the length. */
unsigned gp5_param_format(unsigned id, int32_t value, char *out);

#define GP5_UI_PRESETS 0xFFu          /* a row id: the presets (manifest "presets": N) */
#define GP5_UI_ROWS_MAX 32u
#define GP5_UI_VISIBLE_MAX 8u

typedef struct {
    uint32_t bg, ink, dim, accent, on_accent;          /* 0xRRGGBB */
} gp5_ui_theme_t;

typedef struct {
    /* set by the firmware (designated initialisers; anything left 0 gets a default) */
    const uint8_t *rows;              /* parameter ids (or GP5_UI_PRESETS) in order; 0: every parameter not HIDDEN */
    unsigned n;                       /* how many rows (with rows = 0: worked out) */
    const int16_t *steps;             /* per row, how much one click changes it; 0 or a 0 entry: about 1/50 of the
                                         range, rounded to 1, 2, 5, 10, 20, 50 ... (choices: 1) */
    const gp5_font_t *font;           /* required: e.g. &gp5_font_sans_16 (the firmware includes the font) */
    const gp5_ui_theme_t *theme;      /* 0: dark grey, white, teal */
    const char *title;                /* 0: no title row */
    uint8_t y, h;                     /* the screen area, top and height (0, 0: the whole screen) */
    uint8_t row_h;                    /* row height (0: the font's line height) */
    uint8_t upper;                    /* 1: labels in capitals */
    uint8_t faded;                    /* 1: values drawn dim (e.g. the effect is off); the page redraws when it changes */
    /* the page's own state */
    uint8_t cursor, top, adjusting, preset_pick, msg_on;
    uint32_t msg_until;
    char msg[20];
    char shown[GP5_UI_VISIBLE_MAX + 1][24];
    uint8_t ready, shown_faded;
    uint8_t auto_rows[GP5_UI_ROWS_MAX];
} gp5_ui_page_t;

void gp5_ui_page_turn(gp5_ui_page_t *p, int detents);   /* pick a row, or (adjusting) change its value */
void gp5_ui_page_click(gp5_ui_page_t *p);               /* start / stop adjusting the row picked (read-only: no) */
int gp5_ui_page_draw(gp5_ui_page_t *p, int force);      /* one changed row per call: non-zero while more is left */
void gp5_ui_page_message(gp5_ui_page_t *p, const char *s, unsigned ms);   /* in the last row, for ms */
unsigned gp5_ui_page_row(const gp5_ui_page_t *p);       /* the row picked: its parameter id or GP5_UI_PRESETS */
unsigned gp5_ui_page_preset(const gp5_ui_page_t *p);    /* the preset the presets row shows (save into it on a hold) */

/* A menu: actions to choose from, such as "Save preset", "Reset", "Back". PARA turns to pick an item, a click runs it.
 * An item with confirm asks for a second click first ("Sure?" for 3 s; turning away cancels). An item's value()
 * shows text on the right (what it would do: "P3"). Drawn like the settings page, with the same theme and fonts.
 *
 *     static void save(void) { gp5_preset_save(slot); gp5_ui_menu_message(&menu, "Saved", 1500); }
 *     static const gp5_ui_item_t items[] = { { "Save", save, slot_text, 0 }, { "Reset all", reset, 0, 1 } };
 *     static gp5_ui_menu_t menu = { .items = items, .n = 2, .font = &gp5_font_sans_16, .row_h = 20, .title = "MENU" };
 *
 * A firmware with a page and a menu switches between them itself (a PARA hold, say) and draws the one shown, with
 * force = 1 on the first draw after a switch. */
typedef struct {
    const char *label;
    void (*run)(void);                /* on a click (0: an item that only shows something) */
    const char *(*value)(void);       /* optional: text on the right, asked for whenever the menu draws */
    uint8_t confirm;                  /* 1: run only on a second click */
} gp5_ui_item_t;

typedef struct {
    /* set by the firmware */
    const gp5_ui_item_t *items;
    unsigned n;
    const gp5_font_t *font;           /* required */
    const gp5_ui_theme_t *theme;      /* 0: dark grey, white, teal */
    const char *title;                /* 0: no title row */
    uint8_t y, h, row_h, upper;       /* as the page's */
    /* the menu's own state */
    uint8_t cursor, top, armed, msg_on, ready;
    uint32_t armed_until, msg_until;
    char msg[20];
    char shown[GP5_UI_VISIBLE_MAX + 1][24];
} gp5_ui_menu_t;

void gp5_ui_menu_turn(gp5_ui_menu_t *m, int detents);  /* pick an item (cancels a confirmation) */
int gp5_ui_menu_click(gp5_ui_menu_t *m);               /* run the item picked, or ask to confirm; 1 if it ran */
int gp5_ui_menu_draw(gp5_ui_menu_t *m, int force);      /* one changed row per call: non-zero while more is left */
void gp5_ui_menu_message(gp5_ui_menu_t *m, const char *s, unsigned ms);   /* in the last row, for ms */
unsigned gp5_ui_menu_item(const gp5_ui_menu_t *m);      /* the item picked */
void gp5_ui_menu_pick(gp5_ui_menu_t *m, unsigned i);    /* pick item i (e.g. the first, when the menu opens) */

/* The volume knob takes over a parameter. Until the knob passes the parameter's value nothing changes (no jump);
 * from then on the knob sets it, over its whole range. Call gp5_pickup_poll() in fw_tick. The knob is still the
 * output level unless the firmware owns the level (gp5_level_own(1)). */
typedef struct { uint16_t id; uint8_t live, side; int16_t last; } gp5_pickup_t;
void gp5_pickup_start(gp5_pickup_t *k, unsigned id);
int gp5_pickup_poll(gp5_pickup_t *k);                   /* 1 when it changed the value */
int32_t gp5_pickup_knob_value(const gp5_pickup_t *k);   /* where the knob is, in the parameter's range */
#endif
