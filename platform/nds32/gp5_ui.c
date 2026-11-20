/* OpenGP5 UI kit, control processor (gp5_ui.h; docs/platform/ui.md). Built only for a firmware with params.h. */
#include <stdint.h>
#include "gp5_ui.h"
#ifdef GP5_HAS_PARAMS
#include "gp5_firmware.h"

/* ---- value text ---- */
static unsigned put_s(char *o, unsigned n, const char *s) { while (*s && n < 15u) o[n++] = *s++; o[n] = 0; return n; }
static unsigned put_u(char *o, unsigned n, uint32_t v, unsigned min_digits)
{
    char t[12]; unsigned k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v || k < min_digits);
    while (k && n < 15u) o[n++] = t[--k];
    o[n] = 0; return n;
}
static unsigned put_sign(char *o, unsigned n, int32_t v) { if (v > 0) o[n++] = '+'; else if (v < 0) o[n++] = '-'; return n; }
/* |v| / 10 with one decimal when it has one: 55 -> "5.5", 60 -> "6" */
static unsigned put_tenths(char *o, unsigned n, uint32_t a)
{
    n = put_u(o, n, a / 10u, 1);
    if (a % 10u) { o[n++] = '.'; n = put_u(o, n, a % 10u, 1); }
    return n;
}

unsigned gp5_param_format(unsigned id, int32_t v, char *o)
{
    const gp5_param_desc_t *d;
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    unsigned n = 0;
    o[0] = 0;
    if (id >= GP5_NPARAMS) return 0;
    d = &gp5_param_table[id];
    if (d->flags & GP5_P_CHOICE) {                     /* "A|B|C": the v-th */
        const char *c = d->choices;
        int32_t k = 0;
        while (*c && k < v) { if (*c++ == '|') k++; }
        while (*c && *c != '|' && n < 15u) o[n++] = *c++;
        o[n] = 0;
        return n ? n : put_u(o, 0, a, 1);
    }
    switch (d->unit) {
    case GP5_U_PERCENT: if (v < 0) o[n++] = '-'; n = put_u(o, n, a, 1); return put_s(o, n, " %");
    case GP5_U_DB10: n = put_sign(o, 0, v); n = put_tenths(o, n, a); return put_s(o, n, " dB");
    case GP5_U_MS:
        if (v < 0) o[n++] = '-';
        if (a < 1000u) { n = put_u(o, n, a, 1); return put_s(o, n, " ms"); }
        n = put_u(o, n, a / 1000u, 1);
        if (a % 1000u) { o[n++] = '.'; n = put_u(o, n, (a % 1000u + 5u) / 10u, 2); }   /* 1.50 s, as Launchpad */
        return put_s(o, n, " s");
    case GP5_U_HZ: if (v < 0) o[n++] = '-'; n = put_u(o, n, a, 1); return put_s(o, n, " Hz");
    case GP5_U_SEMITONES: n = put_sign(o, 0, v); n = put_u(o, n, a, 1); return put_s(o, n, " st");
    case GP5_U_CENTS10: n = put_sign(o, 0, v); n = put_tenths(o, n, a); return put_s(o, n, " cents");
    case GP5_U_CUSTOM: {
        char t[16] = { 0 };
        if (fw_param_text(id, v, t)) { t[15] = 0; return put_s(o, 0, t); }
    }   /* fall through: the number */
    default: if (v < 0) o[n++] = '-'; return put_u(o, n, a, 1);
    }
}

/* ---- the settings page ---- */
static const gp5_ui_theme_t default_theme = { 0x101418, 0xF2F4F7, 0x8A93A3, 0x2A9D8F, 0xFFFFFF };

static void setup(gp5_ui_page_t *p)
{
    unsigned k;
    if (p->ready) return;
    if (!p->rows) {
        p->n = 0;
        for (k = 0; k < GP5_NPARAMS && p->n < GP5_UI_ROWS_MAX; k++)
            if (!(gp5_param_table[k].flags & GP5_P_HIDDEN)) p->auto_rows[p->n++] = (uint8_t)k;
        if (gp5_preset_count() && p->n < GP5_UI_ROWS_MAX) p->auto_rows[p->n++] = GP5_UI_PRESETS;
        p->rows = p->auto_rows;
    }
    if (!p->h) { p->y = 0; p->h = GP5_SCREEN_H; }
    if (!p->row_h) p->row_h = p->font->h;
    if (!p->theme) p->theme = &default_theme;
    {   int c = gp5_preset_current(); p->preset_pick = (uint8_t)(c < 0 ? 0 : c); }
    p->ready = 1;
}
static unsigned visible(const gp5_ui_page_t *p)
{
    unsigned v = (p->h - (p->title ? p->row_h : 0u)) / p->row_h;
    return v > GP5_UI_VISIBLE_MAX ? GP5_UI_VISIBLE_MAX : v ? v : 1u;
}
static int32_t nice_step(unsigned id)
{
    const gp5_param_desc_t *d = &gp5_param_table[id];
    int32_t raw = ((int32_t)d->max - d->min) / 50, s = 1;
    if (d->flags & GP5_P_CHOICE) return 1;
    for (;;) {                                         /* 1, 2, 5, 10, 20, 50 ... at most raw */
        if (s * 2 > raw) return s;
        if (s * 5 > raw) return s * 2;
        if (s * 10 > raw) return s * 5;
        s *= 10;
    }
}

unsigned gp5_ui_page_row(const gp5_ui_page_t *p) { return p->n ? p->rows[p->cursor] : 0u; }
unsigned gp5_ui_page_preset(const gp5_ui_page_t *p) { return p->preset_pick; }

void gp5_ui_page_turn(gp5_ui_page_t *p, int d)
{
    unsigned r;
    setup(p);
    if (!p->n || !d) return;
    if (!p->adjusting) {                               /* pick a row (no wrap: the ends are felt) */
        int c = (int)p->cursor + d;
        p->cursor = (uint8_t)(c < 0 ? 0 : c >= (int)p->n ? (int)p->n - 1 : c);
        return;
    }
    r = p->rows[p->cursor];
    if (r == GP5_UI_PRESETS) {
        unsigned cnt = gp5_preset_count();
        if (!cnt) return;
        p->preset_pick = (uint8_t)(((int)p->preset_pick + d % (int)cnt + (int)cnt) % (int)cnt);
        gp5_preset_load(p->preset_pick);               /* an empty one: nothing changes */
        return;
    }
    {
        const gp5_param_desc_t *ds = &gp5_param_table[r];
        int32_t step = p->steps && p->steps[p->cursor] ? p->steps[p->cursor] : nice_step(r);
        int32_t v = gp5_param(r) + step * d;
        if (ds->flags & GP5_P_CHOICE) {                /* choices go round */
            int32_t cnt = ds->max + 1;
            v = ((gp5_param(r) + d) % cnt + cnt) % cnt;
        } else if (step > 1) {                         /* back onto the grid of steps from min: 503 +10 -> 510 */
            int32_t m = ((v - ds->min) % step + step) % step;
            if (m) v += d > 0 ? -m : step - m;
        }
        gp5_param_set(r, v);                           /* clamped there */
    }
}

void gp5_ui_page_click(gp5_ui_page_t *p)
{
    unsigned r;
    setup(p);
    if (!p->n) return;
    r = p->rows[p->cursor];
    if (!p->adjusting && r != GP5_UI_PRESETS && (gp5_param_table[r].flags & GP5_P_READONLY)) return;
    p->adjusting = !p->adjusting;
}

void gp5_ui_page_message(gp5_ui_page_t *p, const char *s, unsigned ms)
{
    unsigned i;
    for (i = 0; s[i] && i < sizeof p->msg - 1u; i++) p->msg[i] = s[i];
    p->msg[i] = 0;
    p->msg_on = 1;
    p->msg_until = gp5_ms() + ms;
}

/* what a row shows, as one string (the cache key): state, label, a TAB, value */
static void row_text(gp5_ui_page_t *p, unsigned r, unsigned sel, char *t)
{
    unsigned n = 0, i;
    char v[16];
    const char *lab;
    t[n++] = (char)('0' + (sel ? (p->adjusting ? 2 : 1) : 0));
    if (r == GP5_UI_PRESETS) {
        unsigned k = p->preset_pick;
        const char *nm = gp5_preset_name(k);
        lab = "Preset";
        i = 0; v[i++] = 'P'; i = put_u(v, i, k + 1u, 1);
        if (!gp5_preset_used(k)) i = put_s(v, i, " empty");
        else {
            if (nm[0]) { v[i++] = ' '; while (*nm && i < 13u) v[i++] = *nm++; }
            if ((int)k == gp5_preset_current() && gp5_preset_dirty()) v[i++] = '*';
            v[i] = 0;
        }
    } else { lab = gp5_param_table[r].name; gp5_param_format(r, gp5_param(r), v); }
    for (i = 0; lab[i] && n < 17u; i++) t[n++] = (char)(p->upper && lab[i] >= 'a' && lab[i] <= 'z' ? lab[i] - 32 : lab[i]);
    t[n++] = '\t';
    for (i = 0; v[i] && n < 23u; i++) t[n++] = v[i];
    t[n] = 0;
}

static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void copy(char *d, const char *s) { while ((*d++ = *s++) != 0) {} }

static uint16_t band[GP5_SCREEN_W * 32];

static void clear_row(const gp5_ui_theme_t *th, unsigned row_h, unsigned y)
{
    unsigned h = row_h > 32u ? 32u : row_h, i;
    for (i = 0; i < GP5_SCREEN_W * h; i++) band[i] = gp5_color(th->bg);
    gp5_blit(0, y, GP5_SCREEN_W, h, band);
}

/* one row: t = state ('0' plain, '1' picked, '2' adjusting), label, TAB, value; kind 1 = title, 2 = message (t is text) */
static void draw_row(const gp5_ui_theme_t *th, const gp5_font_t *f, unsigned row_h, int faded, unsigned y, const char *t, int kind)
{
    unsigned h = row_h > 32u ? 32u : row_h, k, state = kind ? 0u : (unsigned)(t[0] - '0');
    int ty = ((int)h - (int)f->h) / 2 + 1, margin = 6;
    uint32_t bg = th->bg, lab = th->dim, val = faded ? th->dim : th->ink;
    char left[20], right[24];
    unsigned i = 0, j = 0;
    const char *s = kind ? t : t + 1;
    while (*s && *s != '\t' && i < sizeof left - 1u) left[i++] = *s++;
    left[i] = 0;
    if (*s == '\t') s++;
    if (state == 2) { right[j++] = '<'; right[j++] = ' '; }
    while (*s && j < sizeof right - 3u) right[j++] = *s++;
    if (state == 2) { right[j++] = ' '; right[j++] = '>'; }
    right[j] = 0;
    if (kind == 2) { bg = th->accent; lab = th->on_accent; }
    else if (kind == 1) { lab = th->dim; }
    else if (state == 1) { bg = th->accent; lab = val = th->on_accent; }
    else if (state == 2) { lab = th->ink; val = th->accent; }
    for (k = 0; k < GP5_SCREEN_W * h; k++) band[k] = gp5_color(bg);
    if (state == 2 && kind == 0) for (k = 0; k < h; k++) { band[k * GP5_SCREEN_W] = band[k * GP5_SCREEN_W + 1] = band[k * GP5_SCREEN_W + 2] = gp5_color(th->accent); }
    {   /* the value wins: a label that would run into it is cut short */
        int rw = right[0] ? gp5_text_width(f, right) : 0, room = GP5_SCREEN_W - 2 * margin - rw - (rw ? 6 : 0);
        while (i && gp5_text_width(f, left) > room) left[--i] = 0;
        if (i) gp5_draw_text(band, GP5_SCREEN_W, h, margin, ty, f, left, lab);
        if (rw) gp5_draw_text(band, GP5_SCREEN_W, h, GP5_SCREEN_W - margin - rw, ty, f, right, val);
    }
    gp5_blit(0, y, GP5_SCREEN_W, h, band);
}

int gp5_ui_page_draw(gp5_ui_page_t *p, int force)
{
    unsigned vis, k, y0;
    char t[24];
    setup(p);
    vis = visible(p);
    if (p->msg_on && (int32_t)(gp5_ms() - p->msg_until) >= 0) p->msg_on = 0;
    if (p->cursor < p->top) p->top = p->cursor;                     /* keep the row picked in view */
    if (p->cursor >= p->top + vis) p->top = (uint8_t)(p->cursor - vis + 1u);
    if (force || p->faded != p->shown_faded) {
        for (k = 0; k <= GP5_UI_VISIBLE_MAX; k++) p->shown[k][0] = 0x7F;
        p->shown_faded = p->faded;
    }
    y0 = p->y;
    if (p->title) {
        if (p->shown[GP5_UI_VISIBLE_MAX][0] == 0x7F) {
            unsigned i; for (i = 0; p->title[i] && i < 22u; i++) t[i] = p->title[i];
            t[i] = 0;
            draw_row(p->theme, p->font, p->row_h, p->faded, y0, t, 1); p->shown[GP5_UI_VISIBLE_MAX][0] = 0;
            return 1;
        }
        y0 += p->row_h;
    }
    for (k = 0; k < vis; k++) {
        unsigned r = p->top + k;
        int kind = 0;
        if (p->msg_on && k == vis - 1u) { t[0] = '\x01'; copy(t + 1, p->msg); kind = 2; }
        else if (r < p->n) row_text(p, p->rows[r], r == p->cursor, t);
        else t[0] = 0;
        if (same(t, p->shown[k])) continue;
        copy(p->shown[k], t);
        if (kind == 2) draw_row(p->theme, p->font, p->row_h, p->faded, y0 + k * p->row_h, t + 1, 2);
        else if (t[0]) draw_row(p->theme, p->font, p->row_h, p->faded, y0 + k * p->row_h, t, 0);
        else clear_row(p->theme, p->row_h, y0 + k * p->row_h);
        return 1;                                        /* one row per pass */
    }
    return 0;
}

/* ---- the menu ---- */
#define CONFIRM_MS 3000u                               /* a "click again" waits this long */

static void menu_setup(gp5_ui_menu_t *m)
{
    if (m->ready) return;
    if (!m->h) { m->y = 0; m->h = GP5_SCREEN_H; }
    if (!m->row_h) m->row_h = m->font->h;
    if (!m->theme) m->theme = &default_theme;
    m->ready = 1;
}
static unsigned menu_visible(const gp5_ui_menu_t *m)
{
    unsigned v = (m->h - (m->title ? m->row_h : 0u)) / m->row_h;
    return v > GP5_UI_VISIBLE_MAX ? GP5_UI_VISIBLE_MAX : v ? v : 1u;
}

unsigned gp5_ui_menu_item(const gp5_ui_menu_t *m) { return m->cursor; }
void gp5_ui_menu_pick(gp5_ui_menu_t *m, unsigned i) { menu_setup(m); if (i < m->n) m->cursor = (uint8_t)i; m->armed = 0; }

void gp5_ui_menu_turn(gp5_ui_menu_t *m, int d)
{
    int c;
    menu_setup(m);
    if (!m->n || !d) return;
    c = (int)m->cursor + d;
    m->cursor = (uint8_t)(c < 0 ? 0 : c >= (int)m->n ? (int)m->n - 1 : c);
    m->armed = 0;                                      /* turning away cancels a confirmation */
}

int gp5_ui_menu_click(gp5_ui_menu_t *m)
{
    const gp5_ui_item_t *it;
    menu_setup(m);
    if (!m->n) return 0;
    it = &m->items[m->cursor];
    if (!it->run) return 0;
    if (it->confirm && !(m->armed && (int32_t)(gp5_ms() - m->armed_until) < 0)) {
        m->armed = 1; m->armed_until = gp5_ms() + CONFIRM_MS;
        return 0;
    }
    m->armed = 0;
    it->run();
    return 1;
}

void gp5_ui_menu_message(gp5_ui_menu_t *m, const char *s, unsigned ms)
{
    unsigned i;
    for (i = 0; s[i] && i < sizeof m->msg - 1u; i++) m->msg[i] = s[i];
    m->msg[i] = 0;
    m->msg_on = 1;
    m->msg_until = gp5_ms() + ms;
}

static void item_text(gp5_ui_menu_t *m, unsigned r, char *t)
{
    const gp5_ui_item_t *it = &m->items[r];
    const char *v = it->value ? it->value() : "";
    unsigned n = 0, i;
    int sel = r == m->cursor;
    t[n++] = (char)('0' + (sel ? 1 : 0));
    for (i = 0; it->label[i] && n < 17u; i++)
        t[n++] = (char)(m->upper && it->label[i] >= 'a' && it->label[i] <= 'z' ? it->label[i] - 32 : it->label[i]);
    t[n++] = '\t';
    if (sel && m->armed) v = "Sure?";                  /* (a second click runs it) */
    for (i = 0; v && v[i] && n < 23u; i++) t[n++] = v[i];
    t[n] = 0;
}

int gp5_ui_menu_draw(gp5_ui_menu_t *m, int force)
{
    unsigned vis, k, y0;
    char t[24];
    menu_setup(m);
    vis = menu_visible(m);
    if (m->armed && (int32_t)(gp5_ms() - m->armed_until) >= 0) m->armed = 0;
    if (m->msg_on && (int32_t)(gp5_ms() - m->msg_until) >= 0) m->msg_on = 0;
    if (m->cursor < m->top) m->top = m->cursor;
    if (m->cursor >= m->top + vis) m->top = (uint8_t)(m->cursor - vis + 1u);
    if (force) for (k = 0; k <= GP5_UI_VISIBLE_MAX; k++) m->shown[k][0] = 0x7F;
    y0 = m->y;
    if (m->title) {
        if (m->shown[GP5_UI_VISIBLE_MAX][0] == 0x7F) {
            unsigned i; for (i = 0; m->title[i] && i < 22u; i++) t[i] = m->title[i];
            t[i] = 0;
            draw_row(m->theme, m->font, m->row_h, 0, y0, t, 1); m->shown[GP5_UI_VISIBLE_MAX][0] = 0;
            return 1;
        }
        y0 += m->row_h;
    }
    for (k = 0; k < vis; k++) {
        unsigned r = m->top + k;
        int kind = 0;
        if (m->msg_on && k == vis - 1u) { t[0] = '\x01'; copy(t + 1, m->msg); kind = 2; }
        else if (r < m->n) item_text(m, r, t);
        else t[0] = 0;
        if (same(t, m->shown[k])) continue;
        copy(m->shown[k], t);
        if (kind == 2) draw_row(m->theme, m->font, m->row_h, 0, y0 + k * m->row_h, t + 1, 2);
        else if (t[0]) draw_row(m->theme, m->font, m->row_h, 0, y0 + k * m->row_h, t, 0);
        else clear_row(m->theme, m->row_h, y0 + k * m->row_h);
        return 1;
    }
    return 0;
}

/* ---- knob pickup ---- */
int32_t gp5_pickup_knob_value(const gp5_pickup_t *k)
{
    const gp5_param_desc_t *d = &gp5_param_table[k->id];
    return d->min + (int32_t)(((int32_t)gp5_knob() * ((int32_t)d->max - d->min) + 127) / 255);
}
void gp5_pickup_start(gp5_pickup_t *k, unsigned id)
{
    int32_t v;
    k->id = (uint16_t)(id < GP5_NPARAMS ? id : 0u);
    v = gp5_pickup_knob_value(k);
    k->live = v == gp5_param(k->id);
    k->side = v < gp5_param(k->id) ? 1u : 2u;
    k->last = (int16_t)gp5_knob();
}
int gp5_pickup_poll(gp5_pickup_t *k)
{
    int32_t v, cur;
    int kn = (int)gp5_knob();
    if (k->id >= GP5_NPARAMS) return 0;
    if (kn - k->last < 2 && k->last - kn < 2) return 0;          /* the reading wobbles by a step: real moves only */
    k->last = (int16_t)kn;
    v = gp5_pickup_knob_value(k); cur = gp5_param(k->id);
    if (!k->live) {
        unsigned side = v < cur ? 1u : v > cur ? 2u : 0u;
        if (side && side == k->side) return 0;                   /* not there yet */
        k->live = 1;
    }
    return gp5_param_set(k->id, v);
}
#endif
