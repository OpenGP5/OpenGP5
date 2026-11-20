/* OpenGP5 simulator: a whole firmware on the computer (tools/simulate.py builds and runs this; docs/platform/simulator.md).
 *
 * The control half (the firmware's nds32 C files with the platform's parameters, UI kit and text) is this program; the DSP half is a
 * library (sim_dsp.c) loaded beside it. The simulation runs block by block, as the pedal does:
 *   every 10 ms   fw_tick(), and the PARA hold timer (0.6 s)
 *   every block   the control half's main loop (parameters to the DSP, fw_draw), the DSP's fw_process(), the
 *                 output level, and the audio link: the DSP's words go to the control half's on_block(), whose
 *                 words reach the DSP LINK_DELAY blocks later, as on the pedal
 *   events        controls, knob, parameters, presets and screenshots from a script, at their times
 *
 *   sim <dsp library> <events> <in.raw> <out.raw> <shot dir> <seconds> <store file or -> <files dir or ->
 * in.raw: int32 mono; out.raw: int32 left, right. Events, one per line, times in ms (simulate.py writes them):
 *   <ms> foot_down | foot_up | para_down | para_up | turn <n> | knob <0..255> | set <id> <value> |
 *        preset_load <n> | preset_save <n> | shot <name> | storage_changed
 * Prints what happens (LEDs, log lines, the DSP's load) to stderr. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <dirent.h>
#include "gp5_firmware.h"
#include "gp5_fmt.h"
#include "gp5_font.h"
#ifdef GP5_HAS_PARAMS
#include "gp5_params.h"
void gp5_params_init(void);
void gp5_params_poll(void);
int gp5_params_take(uint32_t *id, uint32_t *value);
int gp5_params_set_outside(unsigned id, int32_t value);
int gp5_params_store_changed(uint32_t id);
#endif

#define LINK_DELAY 3u                                /* blocks from the control half's words to the DSP's fw_process */
#define BLOCK 64u

static uint32_t now_ms;
uint32_t gp5_ms(void) { return now_ms; }

/* ---- the firmware's callbacks: empty defaults, as the platform's ---- */
__attribute__((weak)) void fw_init(void) {}
__attribute__((weak)) int fw_draw(int force) { (void)force; return 0; }
__attribute__((weak)) void fw_tick(void) {}
__attribute__((weak)) void fw_foot_down(void) {}
__attribute__((weak)) void fw_foot_up(void) {}
__attribute__((weak)) void fw_para_down(void) {}
__attribute__((weak)) void fw_para_up(void) {}
__attribute__((weak)) void fw_para_click(void) {}
__attribute__((weak)) void fw_para_hold(void) {}
__attribute__((weak)) void fw_para_turn(int detents) { (void)detents; }
__attribute__((weak)) void fw_storage_changed(void) {}
__attribute__((weak)) void fw_dsp_ready(void) {}
__attribute__((weak)) int fw_take(uint32_t *id, uint32_t *value) { (void)id; (void)value; return 0; }
__attribute__((weak)) unsigned fw_setting_count(void) { return 0; }
__attribute__((weak)) int fw_param_get(uint32_t id, uint32_t *value) { (void)id; (void)value; return 0; }
__attribute__((weak)) int fw_param_set(uint32_t id, uint32_t value) { (void)id; (void)value; return 0; }
#ifdef GP5_FW_ID
__attribute__((weak)) const char *fw_name(void) { return GP5_FW_ID; }
#else
__attribute__((weak)) const char *fw_name(void) { return "opengp5"; }
#endif

/* ---- controls, level, LEDs ---- */
static unsigned knob = 255, level, level_owned;
static uint32_t led = GP5_LED_ON;
unsigned gp5_knob(void) { return knob; }
void gp5_level_own(int own) { level_owned = own != 0; }
void gp5_level_set(unsigned l) { level = l > 255u ? 255u : l; }
void gp5_led(uint32_t c) { led = c; }

/* ---- log ---- */
void gp5_log(const char *fmt, ...)
{
    char line[112];
    va_list ap;
    va_start(ap, fmt); gp5_vfmt(line, sizeof line, fmt, ap); va_end(ap);
    fprintf(stderr, "[%5u.%03u] %s\n", now_ms / 1000u, now_ms % 1000u, line);
}

/* ---- screen ---- */
static uint16_t fb[GP5_SCREEN_W * GP5_SCREEN_H];
static unsigned long blit_bytes, blit_most, blit_first;   /* bytes sent to the screen in this fw_draw call; the most in one
                                                                 (and in the first, forced one: a full draw, expected) */
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{
    blit_bytes += 2ul * w * h;
    for (unsigned j = 0; j < h; j++)
        for (unsigned i = 0; i < w; i++)
            if (x + i < GP5_SCREEN_W && y + j < GP5_SCREEN_H) fb[(y + j) * GP5_SCREEN_W + x + i] = px[j * w + i];
}
#include "font5x7.h"
void gp5_text(unsigned line, const char *s)          /* the built-in 5x7 font, 2x, as main.c draws it: only the */
{                                                    /* characters that changed are sent again */
    static uint16_t cell[12 * 16];
    static char shown[5][13];
    uint16_t fg = gp5_color(0xFFFFFF), bg = gp5_color(0);
    int end = 0;
    if (line >= 5) return;
    for (unsigned c = 0; c < 13; c++) {
        char ch = ' ';
        if (!end) { if (s[c]) ch = s[c]; else end = 1; }
        if (ch < 0x20 || ch > GP5_FONT5X7_LAST) ch = '?';
        if (shown[line][c] == ch) continue;
        shown[line][c] = ch;
        for (unsigned y = 0; y < 16; y++)
            for (unsigned x = 0; x < 12; x++) {
                unsigned fx = x / 2, fy = y / 2;
                cell[y * 12 + x] = (fx < 5 && fy < 8 && ((font[ch - 0x20][fx] >> fy) & 1)) ? fg : bg;
            }
        gp5_blit(2 + c * 12, line * 16, 12, 16, cell);
    }
}
static void shot(const char *dir, const char *name)
{
    char p[1024];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    if (!(f = fopen(p, "wb"))) { fprintf(stderr, "can't write %s\n", p); return; }
    fprintf(f, "P6 %d %d 255\n", GP5_SCREEN_W, GP5_SCREEN_H);
    for (int i = 0; i < GP5_SCREEN_W * GP5_SCREEN_H; i++) {      /* the panel's pixels (UI_PANEL): blue in the top bits */
        unsigned v = (uint16_t)((fb[i] >> 8) | (fb[i] << 8)), b = v >> 11, g = (v >> 5) & 63, r = v & 31;
        fputc((int)(r << 3 | r >> 2), f); fputc((int)(g << 2 | g >> 4), f); fputc((int)(b << 3 | b >> 2), f);
    }
    fclose(f);
}

/* ---- storage: records in memory (and a file between runs), files from a folder ---- */
#define NREC 128
static struct { uint32_t id; unsigned len; uint8_t d[GP5_STORE_MAX]; } recs[NREC];
static unsigned nrec;
unsigned gp5_store_load(uint32_t id, void *dst, unsigned max)
{
    for (unsigned i = 0; i < nrec; i++)
        if (recs[i].id == id) { memcpy(dst, recs[i].d, recs[i].len < max ? recs[i].len : max); return recs[i].len; }
    return 0;
}
int gp5_store_save(uint32_t id, const void *src, unsigned len)
{
    unsigned i;
    if (len > GP5_STORE_MAX) return -1;
    for (i = 0; i < nrec && recs[i].id != id; i++) {}
    if (i == NREC) return -1;
    if (i == nrec) nrec++;
    recs[i].id = id; recs[i].len = len; memcpy(recs[i].d, src, len);
    return 0;
}
static void store_file(const char *path, int save)
{
    FILE *f = fopen(path, save ? "wb" : "rb");
    if (!f) return;
    if (save) { fwrite(&nrec, sizeof nrec, 1, f); fwrite(recs, sizeof recs[0], nrec, f); }
    else if (fread(&nrec, sizeof nrec, 1, f) != 1 || nrec > NREC || fread(recs, sizeof recs[0], nrec, f) != nrec) nrec = 0;
    fclose(f);
}
static const char *files_dir;
#define NFILE 64
static struct { char name[32]; uint8_t *d; uint32_t len; } files[NFILE];
static unsigned nfile;
static void load_files(void)
{
    DIR *d;
    struct dirent *e;
    if (!files_dir || !(d = opendir(files_dir))) return;
    while ((e = readdir(d)) && nfile < NFILE) {
        char p[1024]; FILE *f; long n;
        if (e->d_name[0] == '.' || strlen(e->d_name) > 31) continue;
        snprintf(p, sizeof p, "%s/%s", files_dir, e->d_name);
        if (!(f = fopen(p, "rb"))) continue;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        files[nfile].d = malloc(n > 0 ? (size_t)n : 1u);
        if (n >= 0 && fread(files[nfile].d, 1, (size_t)n, f) == (size_t)n) {
            strcpy(files[nfile].name, e->d_name); files[nfile].len = (uint32_t)n; nfile++;
        }
        fclose(f);
    }
    closedir(d);
}
const uint8_t *gp5_file(uint32_t firm, const char *name, uint32_t *len)
{
    (void)firm;
    for (unsigned i = 0; i < nfile; i++) if (!strcmp(files[i].name, name)) { if (len) *len = files[i].len; return files[i].d; }
    return 0;
}
int gp5_file_check(uint32_t firm, const char *name) { return gp5_file(firm, name, 0) != 0; }
unsigned gp5_file_each(uint32_t firm, void (*fn)(const char *, const uint8_t *, uint32_t, void *), void *ctx)
{
    (void)firm;
    for (unsigned i = 0; i < nfile; i++) fn(files[i].name, files[i].d, files[i].len, ctx);
    return nfile;
}

/* ---- the audio link ---- */
static void (*link_cb)(const int32_t *in, int32_t *out);
volatile uint32_t gp5_link_blocks;
void gp5_link_start(void (*on_block)(const int32_t *in, int32_t *out)) { link_cb = on_block; }

/* ---- the DSP half ---- */
static void (*dsp_init)(const uint32_t *);
static int (*dsp_command)(const uint8_t *, unsigned);
static void (*dsp_block)(const int32_t *, int32_t *, int32_t *);
static int32_t *(*dsp_link)(int);
static unsigned (*dsp_link_words)(void), (*dsp_rate)(void);
static int (*dsp_link_early)(const int32_t *);
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void send_setting(uint32_t id, uint32_t v)
{
    uint8_t p[12] = { 0x10, 0x7F, 12, 0 };
    put32(p + 4, id); put32(p + 8, v);
    dsp_command(p, sizeof p);
}

/* one exchange of the audio link: the DSP's words to the control half's on_block(), its words into the delay line */
static void exchange(unsigned words, uint32_t block, int32_t *ctl_in, int32_t *ctl_out, int32_t (*delay)[512])
{
    memcpy(ctl_in, dsp_link(1), words * 4u);
    memset(ctl_out, 0, 512u * 4u);
    link_cb(ctl_in, ctl_out);
    memcpy(delay[block % (LINK_DELAY + 1u)], ctl_out, words * 4u);
    gp5_link_blocks++;
}

static const char *led_text(uint32_t c, char *b)
{
    if (c == GP5_LED_BYPASS) return "dim red";
    if (c == GP5_LED_ON) return "mint";
    snprintf(b, 32, "rgb(%u, %u, %u)", (unsigned)(c & 255u), (unsigned)((c >> 8) & 255u), (unsigned)((c >> 16) & 255u));
    return b;
}

int main(int argc, char **argv)
{
    void *lib;
    FILE *ev, *fin, *fout;
    static int32_t in[BLOCK], l[BLOCK], r[BLOCK], out[2 * BLOCK];
    static int32_t ctl_in[512], ctl_out[512], delay[LINK_DELAY + 1][512], zero[512];
    char line[256], name[200];
    long next_ms = -1; char next_op[32] = ""; long a1 = 0, a2 = 0;
    unsigned words, rate, para_down = 0, para_held = 0, force = 1;
    int early;
    uint32_t para_t0 = 0, next_tick = 10, shown_led = 0xFFFFFFFFu, blocks = 0, total;
    double seconds;
    if (argc != 9) { fprintf(stderr, "usage: sim <dsp library> <events> <in.raw> <out.raw> <shot dir> <seconds> <store|-> <files|->\n"); return 2; }
    if (!(lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL))) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    *(void **)&dsp_init = dlsym(lib, "sim_dsp_init"); *(void **)&dsp_command = dlsym(lib, "sim_dsp_command");
    *(void **)&dsp_block = dlsym(lib, "sim_dsp_block"); *(void **)&dsp_link = dlsym(lib, "sim_dsp_link");
    *(void **)&dsp_link_words = dlsym(lib, "sim_dsp_link_words"); *(void **)&dsp_link_early = dlsym(lib, "sim_dsp_link_early"); *(void **)&dsp_rate = dlsym(lib, "sim_dsp_sample_rate");
    if (!dsp_init || !dsp_link_early || !dsp_command || !dsp_block || !dsp_link || !dsp_link_words || !dsp_rate) { fprintf(stderr, "not a sim_dsp library\n"); return 2; }
    words = dsp_link_words(); rate = dsp_rate(); early = dsp_link_early(0);
    if (words > 512u) words = 512u;
    ev = fopen(argv[2], "r"); fin = fopen(argv[3], "rb"); fout = fopen(argv[4], "wb");
    if (!ev || !fin || !fout) { fprintf(stderr, "can't open the events, in or out file\n"); return 2; }
    seconds = atof(argv[6]);
    total = (uint32_t)(seconds * rate / BLOCK + 0.5);
    if (strcmp(argv[7], "-")) store_file(argv[7], 0);
    if (strcmp(argv[8], "-")) { files_dir = argv[8]; load_files(); }

    /* start-up, as the pedal: parameters (saved values), the DSP, the firmware */
#ifdef GP5_HAS_PARAMS
    gp5_params_init();
#endif
    dsp_init(&now_ms);
    fw_init();
    fw_dsp_ready();

    for (blocks = 0; blocks < total; blocks++) {
        now_ms = (uint32_t)((uint64_t)blocks * BLOCK * 1000u / rate);
        /* events due */
        for (;;) {
            if (next_ms < 0) {
                if (!fgets(line, sizeof line, ev)) { next_ms = 0x7FFFFFFF; break; }
                name[0] = 0; a1 = a2 = 0;
                if (sscanf(line, "%ld %31s", &next_ms, next_op) != 2) { next_ms = -1; continue; }
                if (!strcmp(next_op, "shot")) sscanf(line, "%*ld %*s %199s", name);
                else sscanf(line, "%*ld %*s %ld %ld", &a1, &a2);
            }
            if (next_ms > (long)now_ms) break;
            if (!strcmp(next_op, "foot_down")) fw_foot_down();
            else if (!strcmp(next_op, "foot_up")) fw_foot_up();
            else if (!strcmp(next_op, "para_down")) { para_down = 1; para_held = 0; para_t0 = now_ms; fw_para_down(); }
            else if (!strcmp(next_op, "para_up")) { if (para_down && !para_held) fw_para_click(); para_down = 0; fw_para_up(); }
            else if (!strcmp(next_op, "turn")) fw_para_turn((int)a1);
            else if (!strcmp(next_op, "knob")) knob = (unsigned)(a1 < 0 ? 0 : a1 > 255 ? 255 : a1);
#ifdef GP5_HAS_PARAMS
            else if (!strcmp(next_op, "set")) { if (a1 < GP5_NPARAMS) gp5_params_set_outside((unsigned)a1, (int32_t)a2); else fw_param_set((uint32_t)a1, (uint32_t)a2); }
            else if (!strcmp(next_op, "preset_load")) gp5_preset_load((unsigned)a1);
            else if (!strcmp(next_op, "preset_save")) gp5_preset_save((unsigned)a1);
#else
            else if (!strcmp(next_op, "set")) fw_param_set((uint32_t)a1, (uint32_t)a2);
#endif
            else if (!strcmp(next_op, "storage_changed")) fw_storage_changed();
            else if (!strcmp(next_op, "shot")) { while (fw_draw(force)) force = 0; force = 0; shot(argv[5], name); }
            else fprintf(stderr, "unknown event '%s'\n", next_op);
            next_ms = -1;
        }
        /* the 10 ms clock */
        while (next_tick <= now_ms) {
            fw_tick();
            if (para_down && !para_held && now_ms - para_t0 >= 600u) { para_held = 1; fw_para_hold(); }
            next_tick += 10u;
        }
        /* the main loop: settings to the DSP, then the screen */
        {
            uint32_t id, v;
#ifdef GP5_HAS_PARAMS
            gp5_params_poll();
            while (gp5_params_take(&id, &v)) send_setting(id, v);
#endif
            while (fw_take(&id, &v)) send_setting(id, v);
            for (unsigned k = 0; k < 8u; k++) {          /* (one call is one main-loop pass on the pedal) */
                int more;
                int forced = force;
                blit_bytes = 0; more = fw_draw(force); force = 0;
                if (forced) { if (blit_bytes > blit_first) blit_first = blit_bytes; }
                else if (blit_bytes > blit_most) blit_most = blit_bytes;
                if (!more) break;
            }
        }
        if (led != shown_led) { char b[32]; fprintf(stderr, "[%5u.%03u] LEDs: %s\n", now_ms / 1000u, now_ms % 1000u, led_text(led, b)); shown_led = led; }
        /* the DSP's block */
        size_t got = fread(in, sizeof in[0], BLOCK, fin);
        if (got < BLOCK) memset(in + got, 0, (BLOCK - got) * sizeof in[0]);
        if (link_cb) memcpy(dsp_link(0), blocks >= LINK_DELAY ? delay[(blocks - LINK_DELAY) % (LINK_DELAY + 1u)] : zero, words * 4u);
        if (link_cb && early) { dsp_link_early(in); exchange(words, blocks, ctl_in, ctl_out, delay); }   /* at the block's start */
        dsp_block(in, l, r);
        {
            /* as on the pedal (platform/dsp/audio.h): the level glides, at most 4096 (Q16) a block, from 0 at start */
            static int32_t g_now;
            unsigned lv = level_owned ? level : knob;
            int32_t g = (int32_t)(lv * 65536u / 255u), d = g - g_now;
            if (d > 4096) d = 4096; else if (d < -4096) d = -4096;
            for (unsigned i = 0; i < BLOCK; i++) {
                int32_t gi = g_now + (d * (int32_t)(i + 1)) / 64;
                out[2 * i] = (int32_t)(((int64_t)l[i] * gi) >> 16);
                out[2 * i + 1] = (int32_t)(((int64_t)r[i] * gi) >> 16);
            }
            g_now += d;
        }
        fwrite(out, sizeof out[0], 2 * BLOCK, fout);
        /* the audio link's exchange */
        if (link_cb && !early) exchange(words, blocks, ctl_in, ctl_out, delay);                         /* after the block */
    }
    while (fw_draw(force)) force = 0;
    shot(argv[5], "end");
    fprintf(stderr, "screen: at most %lu bytes sent in one fw_draw call while running (one main-loop pass; the whole screen is 25,600;"
                    " the first full draw: %lu)\n", blit_most, blit_first);
    if (strcmp(argv[7], "-")) store_file(argv[7], 1);
    fclose(fout);
    return 0;
}
