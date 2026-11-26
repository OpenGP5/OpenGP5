/* NAM A2-Lite on both processors, control-processor part: layers A2_SPLIT..end of the model, the output and the cab
 * IR, in the audio link's interrupt. The DSP sends each input block with its layers' residual stream and share of
 * the output; the finished block goes back to the DSP, which plays it (../split.h).
 *
 * Captures come from a bank of NAM_SLOTS storage files "tone-NN" (written by OpenGP5 Launchpad), or the built-in
 * default when the bank is empty. Switching: the output fades out (NAM_FADE_OUT blocks), the main loop loads the new
 * capture (parse, bind, prime this side's layers from the silent state of the DSP's), the DSP gets its copy over the
 * link, and the output fades in (NAM_FADE_IN blocks: this also covers the DSP's layers starting from silence). A
 * capture written into a slot while this runs is picked up within a moment and played.
 *
 * Cab IRs: a bank of NAM_IRS storage files "cab-NN" (format below; OpenGP5 Launchpad writes them); each tone plays
 * the one chosen for it (firmware.c), or none. Played while the cab is on (live setting 0, USB 7E 10; on by default).
 * Read when chosen and re-read when the file changes. */
#pragma GCC optimize("O2")
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "../kernel/a2r.c"
#include "../kernel/a2r_blob.c"
#define CABQ_MAX_TAPS 1024
#define CABQ_CYCLES() __nds32__mfsr(NDS32_SR_PFMC0)
#include "../cab/cabq.h"     /* fixed point: this FPU is too slow for it */
#include "gp5_math.h"
#include "gp5_math.c"               /* the platform's sinf / cosf / expf (tone.h's coefficients, main loop only) */
#include "../tone.h"                /* the noise gate and the 3-band EQ, after the IR */
#include "../default_capture.inc"
#include "../split.h"
#include "../pack24.h"
#include "gp5_firmware.h"
#include "nam.h"

#define NAM_FADE_OUT 8u          /* blocks (1.33 ms each at 48 kHz) */
#define NAM_FADE_IN 40u
#define UNITY 65536

enum { ST_PLAY, ST_FADE, ST_LOAD, ST_SEND };

static a2r_state_t a2, a2pre;
static int32_t a2_mem[34000];           /* histories of layers A2_SPLIT..end (132,660 bytes for A2-Lite) */
static int32_t pre_mem[5120];           /* layers 0..A2_SPLIT-1, only to prime ours (19,500 bytes) */
static a2r_capture_t cap, probe;
static const uint32_t *cap_file;        /* the capture file being played: a slot or the built-in one */
static volatile uint32_t st, gain;      /* gain: Q16, sent to the DSP with every block */
static volatile int want;               /* the slot to load next (-1: built-in) */
static int cur = -2;                    /* the slot playing (-1: built-in) */
static uint32_t woff;                   /* next piece of the file for the DSP */
static uint32_t slot_crc[NAM_SLOTS], slot_bad[NAM_SLOTS];
static uint8_t slot_ok[NAM_SLOTS];
static char slot_name[NAM_SLOTS][25];
static uint32_t in_peak;                /* largest input sample since the screen last asked */
static int32_t ymine[A2R_BLOCK];

/* the tone section (gate + EQ), its settings per tone: firmware.c */
static tone_t tone;
_Static_assert(NAM_GATE_MAX == TONE_GATE_MAX && NAM_EQ_MAX == TONE_EQ_MAX && NAM_EQ_FLAT == TONE_EQ_FLAT, "nam.h and tone.h ranges");
void nam_tone(uint8_t gate, uint8_t bass, uint8_t mid, uint8_t treble) { tone_set(&tone, (tone_set_t){ gate, bass, mid, treble }); }

/* the cab IR */
static cabq_t cab;
static volatile uint32_t cab_on = 1u, cab_ready;   /* ready: cab holds the file's IR (the interrupt may use it) */
static volatile uint32_t cab_auto_off;             /* the IR pushed a block over the limit: switched off (until set again) */
static uint32_t cab_limit;                         /* live setting 1: at most this many taps (0 = all) */
static uint32_t cab_cyc;                           /* the IR's cycles in the last block */
#define NAM_CTL_IRMAX (GP5_CTL_MHZ * 1000000u / GP5_SAMPLE_RATE * 64u * 90u / 100u)   /* model + IR above 90 %: the IR goes off */
static const uint8_t *cab_seen;                    /* the file data last loaded (a new write moves it) */
static uint32_t cab_seen_len;
volatile uint32_t nam_cab_taps;                    /* taps in use, 0 = no IR file */

volatile uint32_t nam_lstm_cycles, nam_bypass, nam_blocks, nam_lost, nam_dsp_cycles, nam_cycles, nam_worst;
#define NAM_CTL_SLOW (GP5_CTL_MHZ * 1000000u / GP5_SAMPLE_RATE * 64u * 95u / 100u)    /* 95 % of a block */
volatile uint32_t nam_a2_worst, nam_bind, nam_slow;
volatile uint32_t nam_lv[3], nam_clip[3];   /* meters (debug readout): peaks |x| since cleared, near-full-scale samples: input, model, after the IR */   /* nam_slow: blocks over 95 %; at 50 the model stops */

#define NAM_FIRM 0x42504D41u                         /* "AMPB" */
static const uint8_t *slot_at(int i, unsigned *max)
{
    char name[8] = "tone-00";
    uint32_t len;
    const uint8_t *p;
    name[5] = (char)('0' + (i + 1) / 10); name[6] = (char)('0' + (i + 1) % 10);
    if ((p = gp5_file(NAM_FIRM, name, &len))) { *max = len; return p; }
    *max = 0;
    return 0;                                         /* no capture in this slot */
}

/* A cab file "cab-NN" (NN = 01..NAM_IRS), written by Launchpad. Two formats, both read:
 *   "CAB1": [0] magic, [1] taps, [2] sample rate (48000), [3] reserved, then taps x float32 (1.1 .. 1.2)
 *   "CAB2": [0] magic, [1] taps, [2] sample rate, [3] scale (float32), then taps x int16 (tap = value x scale), padded
 *           to 4 bytes (1.3: one sector; cabq.h plays the IR in 16 bits anyway)
 * then (Launchpad) its name, 32 bytes UTF-8. The gain is the file's: Launchpad sets its loudest frequency to 0 dB.
 * Any tone plays any IR: firmware.c chooses (nam_ir_select); main loop. */
#define CAB_MAGIC 0x31424143u                        /* "CAB1" */
#define CAB2_MAGIC 0x32424143u                       /* "CAB2" */
static int ir_cur = -1;                              /* the IR chosen (-1: none) */
static uint8_t ir_ok[NAM_IRS];
static char ir_name[NAM_IRS][33];
static float ir_taps[CABQ_MAX_TAPS];                 /* a CAB2 file's taps as float, for cabq_set */
static void ir_file(int i, char *n)                  /* "cab-NN" */
{
    n[0] = 'c'; n[1] = 'a'; n[2] = 'b'; n[3] = '-'; n[4] = (char)('0' + (i + 1) / 10); n[5] = (char)('0' + (i + 1) % 10); n[6] = 0;
}
static uint32_t ir_bytes(const uint32_t *w)          /* the taps' bytes in a file of either format (0: neither) */
{
    if (w[0] == CAB_MAGIC) return 4u * w[1];
    if (w[0] == CAB2_MAGIC) return (2u * w[1] + 3u) & ~3u;
    return 0;
}
static const uint32_t *ir_at(int i, uint32_t *len)   /* IR i's file when its header makes sense, else 0 */
{
    char name[8];
    const uint8_t *f;
    const uint32_t *w;
    ir_file(i, name);
    f = gp5_file(NAM_FIRM, name, len);
    w = (const uint32_t *)f;
    if (!f || *len < 16u || ((uintptr_t)f & 3u) || w[1] == 0 || w[1] > CABQ_MAX_TAPS || !ir_bytes(w) ||
        16u + ir_bytes(w) > *len) return 0;
    return w;
}
static void scan_ir(int i)                           /* is it there, and its name (for the screen) */
{
    uint32_t len = 0, k, n = 0;
    const uint32_t *w = ir_at(i, &len);
    ir_ok[i] = w != 0;
    if (w) {
        const uint8_t *s = (const uint8_t *)w + 16u + ir_bytes(w);
        uint32_t room = len - 16u - ir_bytes(w);
        for (k = 0; k < 32u && k < room && s[k]; k++) {
            if (s[k] >= 0x80u && s[k] < 0xC0u) continue;   /* UTF-8: one ? for each character this font lacks */
            ir_name[i][n++] = s[k] < 0x80u ? (char)s[k] : '?';
        }
    }
    if (!n) { ir_name[i][0] = 'I'; ir_name[i][1] = 'R'; ir_name[i][2] = ' '; ir_name[i][3] = (char)('0' + (i + 1) / 10);
              ir_name[i][4] = (char)('0' + (i + 1) % 10); n = 5; }
    ir_name[i][n] = 0;
}
static void cab_check(void)
{
    uint32_t len = 0;
    char name[8];
    const uint32_t *w = 0;
    if (ir_cur >= 0) { w = ir_at(ir_cur, &len); ir_file(ir_cur, name); }
    if ((const uint8_t *)w == cab_seen && len == cab_seen_len) return;
    cab_ready = 0;                                    /* the interrupt stops using it (plays the model dry) */
    cab_seen = (const uint8_t *)w; cab_seen_len = len; nam_cab_taps = 0;
    if (!w || !gp5_file_check(NAM_FIRM, name)) { cabq_set(&cab, 0, 0); return; }
    {
        uint32_t n = w[1], k;
        const float *h = (const float *)(w + 4);
        if (cab_limit && n > cab_limit) n = cab_limit;
        if (w[0] == CAB2_MAGIC) {
            const int16_t *q = (const int16_t *)(w + 4);
            union { uint32_t u; float f; } sc = { w[3] };
            for (k = 0; k < n; k++) ir_taps[k] = (float)q[k] * sc.f;
            h = ir_taps;
        }
        cabq_set(&cab, h, (int)n);
        nam_cab_taps = n;
    }
    cab_ready = 1;
}
int nam_ir_ok(int i) { return i >= 0 && i < NAM_IRS && ir_ok[i]; }
const char *nam_ir_name(int i) { return i >= 0 && i < NAM_IRS ? ir_name[i] : ""; }
int nam_ir_current(void) { return ir_cur; }
void nam_ir_select(int i) { ir_cur = i >= 0 && i < NAM_IRS ? i : -1; cab_check(); }
void nam_cab(int on) { cab_on = on ? 1u : 0u; cab_auto_off = 0; }
void nam_cab_limit(unsigned taps) { cab_limit = taps; cab_seen = 0; cab_seen_len = 0; cab_check(); }   /* reload */
unsigned nam_cab_get_limit(void) { return cab_limit; }
int nam_cab_is_on(void) { return (int)cab_on; }
unsigned nam_cab_len(void) { return nam_cab_taps; }

/* main loop: load slot `want` (or the built-in one), bind this side's layers and prime them */
static void load(void)
{
    static int32_t z[A2R_BLOCK], x0[A2R_BLOCK * A2R_C], o[A2R_BLOCK];
    int s = want, rf, left;
    unsigned max = 0;
    const uint8_t *f = s >= 0 ? slot_at(s, &max) : 0;
    if (f && a2r_capture_parse(&cap, f, max) == 0) cap_file = (const uint32_t *)f;
    else { s = -1; a2r_capture_parse(&cap, a2r_default_capture, sizeof a2r_default_capture); cap_file = a2r_default_capture; }
    nam_bind = (uint32_t)-a2r_bind_range(&a2, &cap.m, A2_SPLIT, cap.m.nl, a2_mem, sizeof a2_mem);   /* 0 = ok */
    if (!nam_bind && !a2r_bind_range(&a2pre, &cap.m, 0, A2_SPLIT, pre_mem, sizeof pre_mem)) {
        /* NAM's pad_start for our layers: silence through the DSP's layers (their state settles within their
         * receptive field), then that steady stream through ours for the whole receptive field */
        for (rf = cap.m.kh, left = 0; left < cap.m.nl; left++) rf += (cap.m.layers[left].K - 1) * cap.m.layers[left].D;
        for (left = 0; left < 4; left++) a2r_run(&a2pre, z, 0, x0, o, 0);
        for (left = rf - 1; left > 0; left -= A2R_BLOCK) a2r_run(&a2, z, x0, 0, o, 1);
    }
    nam_slow = 0; nam_a2_worst = 0; nam_worst = 0; woff = 0;
    cur = want = s;
}

/* one slot's header: changed? -> check the whole file. Returns 1 when a new capture arrived there. */
static int scan_slot(int i)
{
    unsigned max;
    const uint8_t *f = slot_at(i, &max);
    uint32_t c = max >= 68 ? a2r_capture_crc_field(f) : 0;
    int fresh;
    if (!c) { slot_ok[i] = 0; return 0; }             /* empty, or being written (its header comes last) */
    if ((c == slot_crc[i] && slot_ok[i]) || c == slot_bad[i]) return 0;
    if (a2r_capture_parse(&probe, f, max)) { slot_bad[i] = c; slot_ok[i] = 0; return 0; }
    fresh = c != slot_crc[i];                         /* (the same capture back after its sector was rewritten: not new) */
    slot_crc[i] = c; slot_ok[i] = 1;
    for (int k = 0; k < 25; k++) slot_name[i][k] = probe.name[k];
    return fresh;
}

static void on_block(const int32_t *in, int32_t *out)
{
    static int32_t ys[A2R_BLOCK];
    static int32_t uu[A2R_BLOCK], xx[A2R_BLOCK * A2R_C], yd[A2R_BLOCK];
    uint32_t t0 = __nds32__mfsr(NDS32_SR_PFMC0), t, i, pk = in_peak;
    if (st == ST_LOAD || (uint32_t)in[0] != MAGIC_D || nam_bind) { nam_lost++; out[0] = 0; return; }
    {   /* the DSP's blocks, unpacked (pack24.h) */
        uint32_t s = (uint32_t)in[LW_SHIFTS];
        unsigned su = s & 15u, sx[3] = { (s >> 4) & 15u, (s >> 8) & 15u, (s >> 12) & 15u }, sy = (s >> 16) & 15u;
        p24_unpack(in + LW_U, A2R_BLOCK, &su, 1, uu);
        if (in[LW_DVALID]) { p24_unpack(in + LW_X, A2R_BLOCK * A2R_C, sx, 3, xx); p24_unpack(in + LW_YD, A2R_BLOCK, &sy, 1, yd); }
    }
    for (i = 0; i < A2R_BLOCK; i++) {
        int32_t v = uu[i];
        uint32_t a = v < 0 ? (uint32_t)(-(v + 1)) : (uint32_t)v;
        if (a > pk) pk = a;
    }
    in_peak = pk;
    if (pk > nam_lv[0]) nam_lv[0] = pk;
    for (i = 0; i < A2R_BLOCK; i++) { int32_t v = uu[i]; if (v > 0x7FFF0000 || v < -0x7FFF0000) nam_clip[0]++; }
    if (st == ST_SEND || (uint32_t)in[LW_DSPCRC] != cap.crc) {   /* the DSP does not have this capture yet */
        uint32_t total = cap.length / 4u, n = total - woff;
        if ((uint32_t)in[LW_DSPCRC] == cap.crc) { st = ST_PLAY; gain = 0; }
        else {
            if (n > LW_W_MAX) n = LW_W_MAX;
            out[1] = (int32_t)woff; out[2] = (int32_t)n; out[3] = (int32_t)total; out[4] = (int32_t)cap.crc;
            for (i = 0; i < n; i++) out[LW_W_DATA + i] = (int32_t)cap_file[woff + i];
            out[0] = (int32_t)MAGIC_W;
            woff = woff + n >= total ? 0 : woff + n;
            gain = 0;
            return;
        }
    }
    if (!in[LW_DVALID]) { nam_lost++; out[0] = 0; return; }   /* the DSP's layers did not run on this block */
    if (st == ST_FADE) {
        gain = gain > UNITY / NAM_FADE_OUT ? gain - UNITY / NAM_FADE_OUT : 0;
        if (!gain) st = ST_LOAD;                       /* this block still goes out, silent; the main loop loads */
    } else if (gain < UNITY) {
        gain = gain + UNITY / NAM_FADE_IN < UNITY ? gain + UNITY / NAM_FADE_IN : UNITY;
    }
    if (nam_slow >= 50u) { nam_lost++; out[0] = 0; return; }
    a2r_run(&a2, uu, xx, 0, ymine, 1);
    cab_cyc = 0;
    {   /* the shares added (clamped at full scale), metered: peak since the computer last cleared it, clipped samples */
        uint32_t pm = nam_lv[1];
        for (i = 0; i < A2R_BLOCK; i++) {
            int64_t sm = (int64_t)ymine[i] + yd[i];
            uint64_t a = (uint64_t)(sm < 0 ? -sm : sm);
            if (a > 0x7FFF0000ull) nam_clip[1]++;
            if (a > pm) pm = a > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)a;
            ys[i] = sm > 2147483647LL ? 2147483647 : sm < -2147483648LL ? (int32_t)0x80000000 : (int32_t)sm;
        }
        nam_lv[1] = pm;
    }
    tone_gate(&tone, uu, ys, A2R_BLOCK);                 /* the gate: listens to the input, turns the model down */
    if (cab_on && cab_ready) {
        uint32_t tc = __nds32__mfsr(NDS32_SR_PFMC0);                         /* the cab IR */
        cabq_run(&cab, ys, out + LW_Y);
        cab_cyc = __nds32__mfsr(NDS32_SR_PFMC0) - tc;
    } else {
        for (i = 0; i < A2R_BLOCK; i++) out[LW_Y + i] = ys[i];
    }
    tone_eq(&tone, out + LW_Y, A2R_BLOCK);               /* the EQ, last */
    {   /* the output (after the IR, the gate and the EQ), metered */
        uint32_t po = nam_lv[2];
        for (i = 0; i < A2R_BLOCK; i++) {
            int32_t v = out[LW_Y + i];
            uint32_t a = v < 0 ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v;
            if (a > 0x7FFF0000u) nam_clip[2]++;
            if (a > po) po = a;
        }
        nam_lv[2] = po;
    }
    t = __nds32__mfsr(NDS32_SR_PFMC0) - t0;
    nam_lstm_cycles = t;
    if (cab_cyc && t > NAM_CTL_IRMAX) { cab_on = 0; cab_auto_off = 1; }   /* the IR goes first (the main loop needs time too) */
    else if (t > NAM_CTL_SLOW) nam_slow++;             /* too slow without it: keep USB and the screen alive, stop the model */
    if (nam_blocks > 100 && t > nam_a2_worst) nam_a2_worst = t;
    out[LW_RTAG] = in[LW_TAG];
    out[LW_FLAGS] = (int32_t)((nam_bypass & 1u) | 2u);
    out[LW_SEQ] = (int32_t)(nam_blocks + 1u);          /* the DSP counts repeats / skips */
    out[LW_CTLCYC] = (int32_t)t;
    out[LW_CAPCRC] = (int32_t)cap.crc;
    out[LW_GAIN] = (int32_t)gain;
    out[LW_CAB] = (int32_t)cab_cyc; out[LW_CAB + 1] = (int32_t)cab.cyc_fwd; out[LW_CAB + 2] = (int32_t)cab.cyc_mac;
    out[LW_CAB + 3] = (int32_t)cab.cyc_inv;
    out[LW_CAB + 4] = (int32_t)(nam_cab_taps | (cab_on & 1u) << 16 | (cab_auto_off & 1u) << 17);
    out[0] = (int32_t)MAGIC_N;
    nam_dsp_cycles = (uint32_t)in[LW_DSPCYC];
    nam_cycles = __nds32__mfsr(NDS32_SR_PFMC0) - t0;
    if (nam_blocks > 100 && nam_cycles > nam_worst) nam_worst = nam_cycles;
    nam_blocks++;
}

/* ---- for the screen and controls (main loop) ---- */
int nam_slot_ok(int i) { return i >= 0 && i < NAM_SLOTS && slot_ok[i]; }
const char *nam_slot_name(int i) { return nam_slot_ok(i) ? slot_name[i] : ""; }
int nam_current(void) { return cur; }
uint32_t nam_current_crc(void) { return cap.crc; }   /* the capture playing (the built-in one too) */
const char *nam_name(void) { return cap.name; }
int nam_busy(void) { return st != ST_PLAY; }
unsigned nam_progress(void)                           /* 0..255 through a switch */
{
    uint32_t total = cap.length / 4u;
    if (st == ST_FADE) return 32u - gain / 2048u;
    if (st == ST_LOAD) return 48u;
    if (st == ST_SEND) return total ? 64u + 160u * woff / total : 64u;
    return 255u;
}
unsigned nam_take_peak(void)                          /* 0..2^31 */
{
    unsigned p = in_peak;
    in_peak = 0;
    return p;
}
void nam_select(int slot)
{
    if (slot != -1 && !nam_slot_ok(slot)) return;
    want = slot;
    if (st == ST_PLAY) st = gain ? ST_FADE : ST_LOAD;
}

/* main loop (fw_knob, every 10 ms): a pending load; new captures and IRs in the bank; the IR's file about once a second */
int nam_poll(void)
{
    static unsigned tick;
    int got = -1;
    if (st == ST_LOAD) { load(); st = ST_SEND; }     /* (the new tone's IR: firmware.c chooses it) */
    else if (st == ST_PLAY && want != cur) st = gain ? ST_FADE : ST_LOAD;   /* chosen during a switch */
    /* one file looked up per call (every 10 ms): finding one checks every storage header, more of them with a full
     * bank (30 captures + 12 IRs). In turn: a capture slot, an IR (its name for the screen); once a second the IR
     * playing instead (re-read when its file changed). */
    if (++tick % 100u == 50u) cab_check();
    else if (tick & 1u) {
        static int nir;
        scan_ir(nir);
        nir = (nir + 1) % NAM_IRS;
    } else {
        static int next;
        if (scan_slot(next)) got = next;
        next = (next + 1) % NAM_SLOTS;
    }
    if (got >= 0) { nam_select(got); return got; }    /* just uploaded: play it */
    return -2;
}

/* start on `slot` when it holds a capture, else the first slot that does, else the built-in one */
void nam_start(int slot)
{
    int i;
    __nds32__mtsr(1u, NDS32_SR_PFM_CTL);              /* performance counter 0: count cycles */
    __nds32__isb();
    cabq_tables();
    cab.variant = 1;                                  /* radix-4, one partition per pass: the fastest measured */
    cabq_set(&cab, 0, 0);
    tone_init(&tone);
    for (i = 0; i < NAM_SLOTS; i++) scan_slot(i);
    for (i = 0; i < NAM_IRS; i++) scan_ir(i);
    if (!nam_slot_ok(slot)) for (slot = -1, i = 0; i < NAM_SLOTS && slot < 0; i++) if (slot_ok[i]) slot = i;
    want = slot;
    load();                                           /* (its IR: firmware.c chooses it) */
    st = ST_SEND; gain = 0;
    gp5_link_start(on_block);
}
