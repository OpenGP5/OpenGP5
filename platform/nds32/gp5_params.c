/* OpenGP5 1.0 parameters, control-processor side (gp5_params.h; docs/platform/params.md).
 *
 * Holds the values, sends changed DSP parameters to the DSP (link command 0x7F10 [id][value], one per main-loop
 * pass, the way fw_take did), sends them all again when the DSP (re)starts, saves the current sound 2 s after the
 * last change, and keeps the presets.
 *
 * Storage (gp5_store, owned by the firmware): records whose ids come from the firmware's id (FNV-1a of GP5_FW_ID,
 * low byte cleared): low byte 0xFF = the current sound, 0..63 = preset n. A record holds (key, value) pairs, the
 * key a 16-bit hash of the parameter's name, so a firmware update that adds, removes or reorders parameters keeps
 * what was saved:
 *   [magic "GPV1" u32][current preset i8 (current record only) or 0][name, 12 bytes + NUL (presets) or 13 zeros]
 *   [pair count u16][pairs: key u16, value i16]...
 * Built only for a firmware with params.h (GP5_HAS_PARAMS). */
#include <stdint.h>
#include "gp5_params.h"
#ifdef GP5_HAS_PARAMS
#include "gp5_firmware.h"

#ifndef GP5_PRESETS
#define GP5_PRESETS 0u
#endif
#define REC_MAGIC 0x31565047u                        /* "GPV1" */
#define SAVE_MS 2000u
#define NAME_MAX 12u
#define WORDS ((GP5_NPARAMS + 31u) / 32u)

static int32_t cur[GP5_NPARAMS];
static uint32_t to_dsp[WORDS];                       /* DSP parameters still to send */
static uint32_t save_at;                              /* ms when the current sound is saved (0: nothing to save) */
static int preset_cur = -1, preset_changed;
static uint64_t preset_used;
static char preset_names[GP5_PRESETS ? GP5_PRESETS : 1][NAME_MAX + 1];
static uint16_t keys[GP5_NPARAMS];

typedef struct { uint32_t magic; int8_t preset; char name[NAME_MAX + 1]; uint16_t n; struct { uint16_t key; int16_t v; } p[GP5_NPARAMS]; } rec_t;

static uint32_t id_base(void)
{
    const char *s = fw_name();
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h & 0xFFFFFF00u;
}
static uint16_t key_of(const char *name)
{
    uint32_t h = 2166136261u;
    while (*name) { h ^= (uint8_t)*name++; h *= 16777619u; }
    return (uint16_t)(h ^ (h >> 16));
}
static int32_t clamp(unsigned id, int32_t v)
{
    const gp5_param_desc_t *d = &gp5_param_table[id];
    return v < d->min ? d->min : v > d->max ? d->max : v;
}
static void mark_dsp(unsigned id) { if (gp5_param_table[id].flags & GP5_P_DSP) to_dsp[id / 32u] |= 1u << (id % 32u); }
static void touch(unsigned id)
{
    unsigned f = gp5_param_table[id].flags;
    if (f & (GP5_P_SAVE | GP5_P_PRESET)) { save_at = gp5_ms() + SAVE_MS; if (!save_at) save_at = 1; }
    if (f & GP5_P_PRESET) preset_changed = 1;
}

#ifndef GP5_PARAMS_HOST_TEST                         /* (test/params_test.c brings its own) */
__attribute__((weak)) void fw_param_changed(unsigned id) { (void)id; }
__attribute__((weak)) int fw_param_text(unsigned id, int32_t value, char *text) { (void)id; (void)value; (void)text; return 0; }
#endif

int32_t gp5_param(unsigned id) { return id < GP5_NPARAMS ? cur[id] : 0; }
int gp5_param_set(unsigned id, int32_t value)
{
    if (id >= GP5_NPARAMS) return 0;
    value = clamp(id, value);
    if (value == cur[id]) return 0;
    cur[id] = value; mark_dsp(id); touch(id);
    return 1;
}
/* from outside (the computer, a preset): as gp5_param_set, then the firmware hears of it */
static int set_outside(unsigned id, int32_t value)
{
    if (!gp5_param_set(id, value)) return 0;
    fw_param_changed(id);
    return 1;
}

/* ---- records ---- */
static void apply(const rec_t *r, unsigned want)       /* the pairs of r onto the parameters with a flag in want */
{
    unsigned i, k;
    for (i = 0; i < r->n && i < GP5_NPARAMS; i++)
        for (k = 0; k < GP5_NPARAMS; k++)
            if (keys[k] == r->p[i].key && (gp5_param_table[k].flags & want)) { set_outside(k, r->p[i].v); break; }
}
static unsigned fill(rec_t *r, unsigned want)
{
    unsigned k;
    r->magic = REC_MAGIC; r->n = 0;
    for (k = 0; k < GP5_NPARAMS; k++)
        if (gp5_param_table[k].flags & want) { r->p[r->n].key = keys[k]; r->p[r->n].v = (int16_t)cur[k]; r->n++; }
    return (unsigned)((const uint8_t *)&r->p[r->n] - (const uint8_t *)r);
}
static int load_rec(uint32_t id, rec_t *r)
{
    unsigned len = gp5_store_load(id, r, sizeof *r);
    return len >= 20u && r->magic == REC_MAGIC && r->n <= GP5_NPARAMS && len >= (unsigned)((const uint8_t *)&r->p[r->n] - (const uint8_t *)r);
}

/* start-up: defaults, then what was saved; every DSP parameter to send */
void gp5_params_init(void)
{
    static rec_t r;
    unsigned k;
    for (k = 0; k < GP5_NPARAMS; k++) { keys[k] = key_of(gp5_param_table[k].name); cur[k] = gp5_param_table[k].def; mark_dsp(k); }
    preset_used = 0;
    for (k = 0; k < GP5_PRESETS; k++) {
        preset_names[k][0] = 0;
        if (load_rec(id_base() | k, &r)) { preset_used |= 1ull << k; for (unsigned i = 0; i <= NAME_MAX; i++) preset_names[k][i] = i < NAME_MAX ? r.name[i] : 0; }
    }
    preset_cur = -1;
    if (load_rec(id_base() | 0xFFu, &r)) {
        for (unsigned i = 0; i < r.n; i++)
            for (k = 0; k < GP5_NPARAMS; k++)
                if (keys[k] == r.p[i].key && (gp5_param_table[k].flags & (GP5_P_SAVE | GP5_P_PRESET))) { cur[k] = clamp(k, r.p[i].v); break; }
        if (r.preset >= 0 && (unsigned)r.preset < GP5_PRESETS) preset_cur = r.preset;
    }
    preset_changed = 0; save_at = 0;
}
/* every main-loop pass: save the current sound when it has been still for 2 s */
void gp5_params_poll(void)
{
    static rec_t r;
    unsigned len;
    if (!save_at || (int32_t)(gp5_ms() - save_at) < 0) return;
    save_at = 0;
    len = fill(&r, GP5_P_SAVE | GP5_P_PRESET);
    r.preset = (int8_t)preset_cur; for (unsigned i = 0; i <= NAME_MAX; i++) r.name[i] = 0;
    gp5_store_save(id_base() | 0xFFu, &r, len);
}
/* the next DSP parameter to send (fw_take's place); 0 when none */
int gp5_params_take(uint32_t *id, uint32_t *value)
{
    unsigned w, b;
    for (w = 0; w < WORDS; w++)
        if (to_dsp[w]) {
            b = (unsigned)__builtin_ctz(to_dsp[w]);
            to_dsp[w] &= ~(1u << b);
            *id = w * 32u + b; *value = (uint32_t)cur[*id];
            return 1;
        }
    return 0;
}
void gp5_params_dsp_restart(void) { for (unsigned k = 0; k < GP5_NPARAMS; k++) mark_dsp(k); }
/* the DSP parameters among ids 0..31 (the start-up waits until each was acknowledged once) */
uint32_t gp5_params_dsp_mask(void)
{
    uint32_t m = 0;
    for (unsigned k = 0; k < GP5_NPARAMS && k < 32u; k++) if (gp5_param_table[k].flags & GP5_P_DSP) m |= 1u << k;
    return m;
}
/* the computer: set (refused for READONLY), read */
int gp5_params_set_outside(unsigned id, int32_t value)
{
    if (id >= GP5_NPARAMS || (gp5_param_table[id].flags & GP5_P_READONLY)) return 0;
    set_outside(id, value);
    return 1;
}
/* the computer rewrote a store record: 1 if it was one of ours (then everything is read again) */
int gp5_params_store_changed(uint32_t id)
{
    if ((id & 0xFFFFFF00u) != id_base()) return 0;
    {
        static rec_t r;
        unsigned k, low = id & 0xFFu;
        if (low == 0xFFu) {                          /* the current sound, as saved: also which preset it is, unchanged */
            if (load_rec(id, &r)) {
                apply(&r, GP5_P_SAVE | GP5_P_PRESET);
                preset_cur = r.preset >= 0 && (unsigned)r.preset < GP5_PRESETS ? r.preset : -1;
                preset_changed = 0;
            }
        }
        else if (low < GP5_PRESETS) {
            if (load_rec(id, &r)) { preset_used |= 1ull << low; for (k = 0; k <= NAME_MAX; k++) preset_names[low][k] = k < NAME_MAX ? r.name[k] : 0; }
            else { preset_used &= ~(1ull << low); preset_names[low][0] = 0; }
        }
    }
    return 1;
}

/* ---- presets ---- */
unsigned gp5_preset_count(void) { return GP5_PRESETS; }
int gp5_preset_current(void) { return preset_cur; }
int gp5_preset_used(unsigned n) { return n < GP5_PRESETS && ((preset_used >> n) & 1u); }
int gp5_preset_dirty(void) { return preset_changed; }
const char *gp5_preset_name(unsigned n) { return n < GP5_PRESETS ? preset_names[n] : ""; }
int gp5_preset_load(unsigned n)
{
    static rec_t r;
    if (!gp5_preset_used(n) || !load_rec(id_base() | n, &r)) return 0;
    apply(&r, GP5_P_PRESET);
    preset_cur = (int)n; preset_changed = 0;
    save_at = gp5_ms() + SAVE_MS; if (!save_at) save_at = 1;   /* remember which preset is loaded */
    return 1;
}
static int save_preset(unsigned n, const char *name)
{
    static rec_t r;
    unsigned len, i;
    if (n >= GP5_PRESETS) return 0;
    len = fill(&r, GP5_P_PRESET);
    r.preset = 0;
    for (i = 0; i <= NAME_MAX; i++) r.name[i] = 0;
    for (i = 0; i < NAME_MAX && name[i]; i++) r.name[i] = (name[i] >= 32 && name[i] < 127) ? name[i] : '?';
    if (gp5_store_save(id_base() | n, &r, len)) return 0;
    preset_used |= 1ull << n;
    for (i = 0; i <= NAME_MAX; i++) preset_names[n][i] = r.name[i];
    return 1;
}
int gp5_preset_save(unsigned n)
{
    char keep[NAME_MAX + 1];
    unsigned i;
    if (n >= GP5_PRESETS) return 0;
    for (i = 0; i <= NAME_MAX; i++) keep[i] = preset_names[n][i];
    if (!save_preset(n, keep)) return 0;
    preset_cur = (int)n; preset_changed = 0;
    save_at = gp5_ms() + SAVE_MS; if (!save_at) save_at = 1;
    return 1;
}
int gp5_preset_rename(unsigned n, const char *name)
{
    static rec_t r;
    unsigned i;
    if (!gp5_preset_used(n) || !load_rec(id_base() | n, &r)) return 0;
    for (i = 0; i <= NAME_MAX; i++) r.name[i] = 0;
    for (i = 0; i < NAME_MAX && name[i]; i++) r.name[i] = (name[i] >= 32 && name[i] < 127) ? name[i] : '?';
    if (gp5_store_save(id_base() | n, &r, (unsigned)((const uint8_t *)&r.p[r.n] - (const uint8_t *)&r))) return 0;
    for (i = 0; i <= NAME_MAX; i++) preset_names[n][i] = r.name[i];
    return 1;
}
/* a preset's raw record for the computer (backups) / written back from it */
unsigned gp5_preset_record_id(unsigned n) { return id_base() | (n & 0xFFu); }
#endif
