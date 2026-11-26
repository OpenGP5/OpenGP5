/* NAM A2-Lite on both processors, DSP part (see ../split.h for the split and the link words).
 * Each block the DSP runs layers 0..A2_SPLIT-1 on the guitar input and sends the input, the residual stream and its
 * share of the output to the control processor (in the exchange at the start of the next block). The control
 * processor runs the rest, adds the shares, applies the cab IR and sends the finished block back; the DSP plays it
 * (ramped by the control processor's gain) or, in bypass, the dry input. Latency: about three blocks (4 ms at
 * 48 kHz) plus the converters.
 * Its layers start from silence (no prime): their receptive field is 138 samples, inside the fade-in.
 * The capture comes from the control processor over the link (split.h); the DSP holds no model of its own.
 * gp5_link.py raw 307f0400 -> [model cycles last][worst][sequence errors][slow blocks | bind error << 16][dup tags]
 *                                [old tags][capture checksum held][capture pieces received][bad captures]
 *                  raw 317f0400 -> link check (blocks without the control processor's words, repeats, skips, ...)
 *                  raw 327f0400 -> the control processor's cycles: [last][worst][per block] */
/* The audio link (264 words, exchanged at the start of each block), 48 kHz and the control processor at 320 MHz:
 * ../firmware.json (OpenGP5 1.0). */
#define NAM_DSP_SLOW (191900000u / GP5_SAMPLE_RATE * 64u * 95u / 100u)   /* 95 % of a block, core cycles */

#include "../split.h"
#include "../pack24.h"
#include "../kernel/a2r.c"
#include "../kernel/a2r_blob.c"

static a2r_state_t a2 GP5_LARGE;
static a2r_capture_t cap GP5_LARGE;
static uint32_t cfile[2048] GP5_LARGE;           /* the capture file as received (8 KB at most) */
static uint32_t have_crc, rx_crc, rx_next, rx_total, rx_pieces, rx_bad;
static int32_t a2_mem[6144] GP5_LARGE;           /* histories of layers 0..A2_SPLIT-1 (19,500 bytes for A2-Lite) */
static uint32_t blk, tag_last, bind_err;
static int32_t g_prev;                           /* output gain at the end of the last block, Q16 */
static uint32_t cyc, worst, seq_bad, seq_last, slow, d_tagdup, d_tagold;
static uint32_t ctl_cyc, ctl_worst;              /* the control processor's cycles (LW_CTLCYC), for 0x7F32 */
static uint32_t cabw[5];                          /* the cab IR's cycles and state (LW_CAB), for 0x7F33 */
static uint32_t d_notok, d_dup, d_skip, d_since, d_gi, d_gap[8];

/* its parameters (../params.h) are all the control processor's: nothing to do here */
static void fw_param(uint32_t id, int32_t value) { (void)id; (void)value; }

static void fw_init(void)
{
    blk = tag_last = 0;
    cyc = worst = seq_bad = seq_last = slow = d_tagdup = d_tagold = 0;
    d_notok = d_dup = d_skip = d_since = d_gi = 0;
    g_prev = 0; bind_err = 0; have_crc = rx_crc = rx_next = rx_total = rx_pieces = rx_bad = 0;
    ctl_cyc = ctl_worst = 0;
}

/* a piece of the capture file from the control processor; when the last one is in: check it, bind the layers */
static void take_piece(const int32_t *li)
{
    uint32_t off = (uint32_t)li[1], n = (uint32_t)li[2], total = (uint32_t)li[3], crc = (uint32_t)li[4], i;
    if (n > LW_W_MAX || total > 2048u || off + n > total || crc == have_crc) return;   /* (pieces still on the way) */
    if (off == 0) { rx_crc = crc; rx_total = total; rx_next = 0; }
    if (crc != rx_crc || off != rx_next || total != rx_total) return;   /* missed one: wait for the next round */
    for (i = 0; i < n; i++) cfile[off + i] = (uint32_t)li[LW_W_DATA + i];
    rx_next += n; rx_pieces++;
    if (rx_next < rx_total) return;
    rx_crc = 0;
    if (a2r_capture_parse(&cap, cfile, rx_total * 4u) || cap.crc != crc) { rx_bad++; return; }
    bind_err = (uint32_t)-a2r_bind_range(&a2, &cap.m, 0, A2_SPLIT, a2_mem, sizeof a2_mem);
    have_crc = bind_err ? 0 : crc;
    slow = 0; worst = 0; ctl_worst = 0;
}

/* Nothing is ready at the start of a block: everything for the control processor comes out of fw_process(). */
static void fw_link_early(const int32_t *in, int32_t *lo) { (void)in; (void)lo; blk++; }

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    const int32_t *li = gp5_link_in();
    int32_t *lo = gp5_link_out();                  /* goes out at the start of the next block */
    int ok = (uint32_t)li[0] == MAGIC_N;
    if ((uint32_t)li[0] == MAGIC_W) take_piece(li);
    uint32_t flags = ok ? (uint32_t)li[LW_FLAGS] : 0, i, t0 = GP5_CYCLE_COUNT();
    int have = 0;
    {
        int bad = !ok;
        if (!ok) d_notok++;
        else {
            uint32_t q = (uint32_t)li[LW_SEQ];
            ctl_cyc = (uint32_t)li[LW_CTLCYC];
            for (i = 0; i < 5; i++) cabw[i] = (uint32_t)li[LW_CAB + i];
            if (blk > 100u && ctl_cyc > ctl_worst && ctl_cyc < 10000000u) ctl_worst = ctl_cyc;
            if (q != seq_last + 1u) { seq_bad++; bad = 1; if (q == seq_last) d_dup++; else d_skip++; }
            seq_last = q;
        }
        d_since++;
        if (bad) { d_gap[d_gi++ & 7u] = d_since; d_since = 0; }
    }
    /* the control processor's finished block (for an input a few blocks back) */
    if (ok && (flags & 2u) && have_crc && (uint32_t)li[LW_CAPCRC] == have_crc) {
        uint32_t tag = (uint32_t)li[LW_RTAG];
        if (tag == tag_last) d_tagdup++;                       /* the same block again: play nothing new */
        else if (blk - tag >= 8u) d_tagold++;
        else have = 1;
        tag_last = tag;
    }
    /* this block's input through layers 0..A2_SPLIT-1, for the control processor, packed (pack24.h) */
    {
        static int32_t xs[GP5_BLOCK * 3], ys[GP5_BLOCK];
        unsigned su = p24_shift(in, GP5_BLOCK, 0, 1), sx[3] = { 0, 0, 0 }, sy = 0;
        p24_pack(in, GP5_BLOCK, &su, 1, &lo[LW_U]);
        lo[LW_DVALID] = 0;
        if (have_crc && slow < 50u) {
            a2r_run(&a2, in, 0, xs, ys, 0);
            for (i = 0; i < 3; i++) sx[i] = p24_shift(xs, GP5_BLOCK * 3, (int)i, 3);
            sy = p24_shift(ys, GP5_BLOCK, 0, 1);
            p24_pack(xs, GP5_BLOCK * 3, sx, 3, &lo[LW_X]);
            p24_pack(ys, GP5_BLOCK, &sy, 1, &lo[LW_YD]);
            lo[LW_DVALID] = 1;
        }
        lo[LW_SHIFTS] = (int32_t)(su | sx[0] << 4 | sx[1] << 8 | sx[2] << 12 | sy << 16);
    }
    lo[LW_TAG] = (int32_t)blk;
    lo[LW_DSPCRC] = (int32_t)have_crc;
    lo[0] = (int32_t)MAGIC_D;
    cyc = GP5_CYCLE_COUNT() - t0;
    if (cyc > worst && blk > 100u) worst = cyc;
    if (cyc > NAM_DSP_SLOW) slow++;                            /* over 95 %: at 50 its layers stop (silent) */
    lo[LW_DSPCYC] = (int32_t)cyc;
    {   /* play: the control processor's gain (fades when switching captures), ramped over the block */
        int32_t g1 = have ? li[LW_GAIN] : 0, g0 = g_prev;
        if (g1 < 0) g1 = 0;
        if (g1 > 65536) g1 = 65536;
        for (i = 0; i < GP5_BLOCK; i++) {
            int32_t v = 0;
            if (flags & 1u) v = in[i];
            else if (have) {
                int64_t s = li[LW_Y + i];
                int32_t g = g0 + (int32_t)(((g1 - g0) * (int32_t)(i + 1)) / (int32_t)GP5_BLOCK);
                if (g < 65536) s = (s * g) >> 16;
                v = (int32_t)s;
            }
            out_left[i] = v; out_right[i] = v;
        }
        g_prev = (flags & 1u) ? g_prev : g1;
    }
}

static int fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n)
{
    (void)payload; (void)n;
    if (cmd == 0x7F30) {   /* [cmd][len 40][cycles last][worst][sequence errors][slow | bind error << 16][dup tags]
                              [old tags][capture checksum][pieces received][bad captures] */
        uint32_t v[9] = { cyc, worst, seq_bad, slow | (bind_err << 16), d_tagdup, d_tagold, have_crc, rx_pieces, rx_bad }, k;
        uint8_t r[40] = { 0x30, 0x7F, 40, 0 };
        for (k = 0; k < 36; k++) r[4 + k] = (uint8_t)(v[k / 4] >> (8 * (k % 4)));
        gp5_reply(r, sizeof r);
        return 1;
    }
    if (cmd == 0x7F32) {   /* the control processor's load: [cmd][len 16][cycles last][worst][cycles per block] */
        uint32_t v[3] = { ctl_cyc, ctl_worst, GP5_CTL_MHZ * 1000000u / GP5_SAMPLE_RATE * 64u }, k;
        uint8_t r[16] = { 0x32, 0x7F, 16, 0 };
        for (k = 0; k < 12; k++) r[4 + k] = (uint8_t)(v[k / 4] >> (8 * (k % 4)));
        gp5_reply(r, sizeof r);
        return 1;
    }
    if (cmd == 0x7F33) {   /* the cab IR: [cmd][len 24][cycles][forward FFT][products][inverse FFT][taps | on << 16 | auto-off << 17] */
        uint8_t r[24] = { 0x33, 0x7F, 24, 0 };
        uint32_t k;
        for (k = 0; k < 20; k++) r[4 + k] = (uint8_t)(cabw[k / 4] >> (8 * (k % 4)));
        gp5_reply(r, sizeof r);
        return 1;
    }
    if (cmd == 0x7F31) {   /* link check: [cmd][len 48][not ok][repeats][skips][exchanges][busy][no sync][lag][last 5 gaps] */
        uint32_t v[12] = { d_notok, d_dup, d_skip, ST[21], ST[22], ST[23], link_lag }, k;
        uint8_t r[52] = { 0x31, 0x7F, 52, 0 };
        for (k = 0; k < 5; k++) v[7 + k] = d_gap[(d_gi - 1u - k) & 7u];
        for (k = 0; k < 48; k++) r[4 + k] = (uint8_t)(v[k / 4] >> (8 * (k % 4)));
        gp5_reply(r, sizeof r);
        return 1;
    }
    return 0;
}
