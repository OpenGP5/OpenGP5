/* Strobe tuner core, DSP side (no hardware access: tools/../firmwares/tuner/test runs it on the computer).
 *
 * Two parts:
 *  - Which note: YIN pitch detection on the signal low-passed (~1.2 kHz) and decimated 8x (5512 Hz), a 256-sample
 *    window, lags 5..190: 29..1100 Hz, from a 5-string bass's low B (31 Hz) to the guitar's 12th-fret harmonics.
 *    One analysis every 16 blocks (23 ms), its work spread evenly over those blocks: ~3,000 multiply-adds a block.
 *  - The strobe, as a mechanical strobe tuner makes it: a disc turns once per period of the note's exact
 *    frequency, and a lamp flashes with the string. Here the "disc" is a phase counter split into TN_SEG
 *    segments; each band filters out one harmonic (1st, 2nd, 4th), the lamp is on while that harmonic is above
 *    zero, and each segment keeps how much of its time the lamp was on (with a short afterglow). In tune, the
 *    pattern of light and dark segments stands still; sharp, it drifts forwards; flat, backwards. Band b shows
 *    1, 2 and 4 light segments-groups per turn.
 *  - How far off, as a number: the fundamental is also multiplied by a reference oscillator at the note's
 *    frequency and low-passed; the phase of the result turns at the difference frequency. Its rate, averaged over
 *    at least 46 ms and 4 periods of the note once it is steady, gives the cents. */
#ifndef TUNER_H
#define TUNER_H
#include <stdint.h>

#define TN_FS       44100.0f
#define TN_DEC      8u
#define TN_FD       (TN_FS / TN_DEC)
#define TN_W        256u              /* YIN window (decimated samples, 46 ms: more than one period of low B) */
#define TN_LAG_MIN  5u
#define TN_LAG_MAX  190u
#define TN_BUF      512u              /* decimated history, power of two, >= TN_W + TN_LAG_MAX */
#define TN_CYCLE    16u               /* blocks per analysis (23 ms) */
#define TN_ROWS     1u                /* phase measurement (cents): the fundamental */
#define TN_BANDS    3u                /* strobe bands: harmonic 1, 2, 4 */
#define TN_SEG      64u               /* strobe segments per turn */
#define TN_PI       3.14159265f

typedef struct {
    /* in */
    float a4;                         /* reference, Hz */
    /* out */
    int note;                         /* MIDI note (E2 = 40, A4 = 69), -1 = none */
    float cents;                      /* deviation from the note, from the strobe */
    float phase[TN_ROWS];             /* strobe position per row, 0 .. 1 (one stripe) */
    float amp[TN_ROWS];               /* how strong each row's harmonic is, 0 .. 1 */
    float level_db;                   /* input level, dBFS */
    float freq;                       /* detected frequency (Hz), 0 = none */
    /* state */
    float dc_x, dc_y, acc, lp1, lp2;                  /* lp: anti-alias low-pass before the 8x decimation */
    unsigned dec_n, wpos, blk;
    float hist[TN_BUF];
    float work[TN_W + TN_LAG_MAX];
    float d[TN_LAG_MAX + 1];
    float rms2;
    int cand_note, cand_count;
    float ref;                        /* reference frequency of `note` */
    float osc_c[TN_ROWS], osc_s[TN_ROWS], rot_c[TN_ROWS], rot_s[TN_ROWS];
    float i1[TN_ROWS], q1[TN_ROWS], i2[TN_ROWS], q2[TN_ROWS];
    float last_ph[TN_ROWS], ph_acc, ph_n;
    float sig_pow, ref_rows_pow[TN_ROWS];
    unsigned settle, settle_n, meas_n;               /* blocks to wait after a new note / to average the cents over */
    float lpa;                                         /* the phase measurement's low-pass, scaled to the note */
    /* strobe */
    float sph, sstep;                                  /* disc position 0..1, per sample */
    float bc[TN_BANDS], br2[TN_BANDS], bx1, bx2, by1[TN_BANDS], by2[TN_BANDS];   /* two band-passes in series */
    float cx1[TN_BANDS], cx2[TN_BANDS], cy1[TN_BANDS], cy2[TN_BANDS];
    uint16_t on[TN_BANDS][TN_SEG], cnt[TN_SEG];
    float seg[TN_BANDS][TN_SEG];                       /* what the bands show, 0 (dark) .. 1 (lit) */
} tuner_t;

static float tn_atan2(float y, float x)          /* error < 0.005 rad */
{
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a, s, r;
    if (ax < 1e-20f && ay < 1e-20f) return 0.0f;
    a = ax > ay ? ay / ax : ax / ay;
    s = a * a;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = TN_PI - r;
    return y < 0 ? -r : r;
}

static void tuner_set_ref(tuner_t *t, int note)
{
    unsigned k;
    t->ref = t->a4 * exp2f((float)(note - 69) / 12.0f);
    for (k = 0; k < TN_ROWS; k++) {
        float w = 2.0f * TN_PI * t->ref * (float)(1u << k) / TN_FS;
        t->rot_c[k] = cosf(w); t->rot_s[k] = sinf(w);
        t->i1[k] = t->q1[k] = t->i2[k] = t->q2[k] = 0.0f;
    }
    t->ph_acc = 0.0f; t->ph_n = 0.0f; t->settle = 0;
    {   /* the cents measurement: low-pass at most a sixth of the note (10 Hz above 60 Hz), averaged over at least
         * 4 periods; waits 3 low-pass time constants after a new note */
        float fc = t->ref / 6.0f, per;
        if (fc > 10.0f) fc = 10.0f;
        t->lpa = 1.0f - expf(-2.0f * TN_PI * fc / TN_FS);
        per = 4.0f * TN_FS / (t->ref * 64.0f);
        t->meas_n = per > 32.0f ? (unsigned)per + 1u : 32u;
        t->settle_n = (unsigned)(3.0f / (2.0f * TN_PI * fc) * TN_FS / 64.0f) + 1u;
    }
    t->sstep = t->ref / TN_FS;
    for (k = 0; k < TN_BANDS; k++) {                  /* band-pass around harmonic 1, 2, 4: about a third wide */
        float f = t->ref * (float)(1u << k), r = 1.0f - TN_PI * 0.3f * f / TN_FS;
        if (r < 0.9f) r = 0.9f;
        t->bc[k] = 2.0f * r * cosf(2.0f * TN_PI * f / TN_FS);
        t->br2[k] = r * r;
        t->by1[k] = t->by2[k] = t->cx1[k] = t->cx2[k] = t->cy1[k] = t->cy2[k] = 0.0f;
    }
}

static void tuner_init(tuner_t *t, float a4)
{
    unsigned k;
    for (k = 0; k < sizeof *t; k++) ((uint8_t *)t)[k] = 0;
    t->a4 = a4; t->note = -1; t->cand_note = -1; t->level_db = -120.0f;
    for (k = 0; k < TN_ROWS; k++) { t->osc_c[k] = 1.0f; t->osc_s[k] = 0.0f; }
    tuner_set_ref(t, 69);
}

/* the analysis, a slice per block: blk 0 takes the window, blocks 0..7 compute the difference function, block 7
 * decides */
static void tuner_analyse(tuner_t *t)
{
    unsigned b = t->blk % TN_CYCLE, per = (TN_LAG_MAX + TN_CYCLE) / TN_CYCLE, lo = b * per, hi = lo + per, tau, j;
    if (b == 0) {
        unsigned n = TN_W + TN_LAG_MAX, start = (t->wpos - n) & (TN_BUF - 1u);
        float e = 0.0f;
        for (j = 0; j < n; j++) t->work[j] = t->hist[(start + j) & (TN_BUF - 1u)];
        for (j = 0; j < TN_W; j++) e += t->work[j] * t->work[j];
        t->rms2 = e / (float)TN_W;
    }
    if (hi > TN_LAG_MAX + 1u) hi = TN_LAG_MAX + 1u;
    for (tau = lo; tau < hi; tau++) {
        float s = 0.0f;
        if (tau == 0) { t->d[0] = 0.0f; continue; }
        for (j = 0; j < TN_W; j++) { float x = t->work[j] - t->work[j + tau]; s += x * x; }
        t->d[tau] = s;
    }
    if (b != TN_CYCLE - 1u) return;

    /* decide: cumulative mean normalised difference, first dip under the threshold, parabolic refinement */
    {
        float sum = 0.0f, best = 1e30f, f = 0.0f;
        unsigned best_tau = 0;
        int note = -1;
        t->level_db = t->rms2 > 1e-12f ? 10.0f * log10f(t->rms2) + 3.01f : -120.0f;   /* sine peak = 0 dBFS */
        for (tau = 1; tau <= TN_LAG_MAX; tau++) {
            sum += t->d[tau];
            t->d[tau] = sum > 0.0f ? t->d[tau] * (float)tau / sum : 1.0f;
        }
        for (tau = TN_LAG_MIN; tau < TN_LAG_MAX; tau++) {
            if (t->d[tau] < 0.15f) {
                while (tau + 1u < TN_LAG_MAX && t->d[tau + 1u] < t->d[tau]) tau++;
                best_tau = tau; best = t->d[tau];
                break;
            }
            if (t->d[tau] < best) { best = t->d[tau]; best_tau = tau; }
        }
        if (t->level_db > -60.0f && best_tau > TN_LAG_MIN && best_tau < TN_LAG_MAX && best < 0.35f) {
            float a = t->d[best_tau - 1u], c = t->d[best_tau + 1u], den = a - 2.0f * best + c;
            float x = (float)best_tau + (den > 1e-9f ? 0.5f * (a - c) / den : 0.0f);
            f = TN_FD / x;
            note = (int)floorf(69.0f + 12.0f * log2f(f / t->a4) + 0.5f);
        }
        t->freq = f;
        if (note >= 0 && t->note >= 0) {                     /* far off: the pitch estimate's cents (octave-free) */
            float cy = 1200.0f * log2f(f / t->ref);
            while (cy > 600.0f) cy -= 1200.0f;
            while (cy < -600.0f) cy += 1200.0f;
            /* the strobe is the measure, except when it has too little to go on or the string is not (yet) on
             * this note: then the pitch estimate's cents, and the strobe's average starts again */
            if (cy > 50.0f || cy < -50.0f || ((cy > 20.0f || cy < -20.0f) && t->amp[0] < 0.1f)) {
                t->cents = cy > 99.0f ? 99.0f : cy < -99.0f ? -99.0f : cy;
                t->ph_acc = 0.0f; t->ph_n = 0.0f;
            }
        }
        if (note < 0) {                                       /* silence or noise: let go after ~70 ms */
            if (++t->cand_count > 4) { t->note = -1; t->cand_note = -1; }
            return;
        }
        if (note == t->note) { t->cand_count = 0; return; }  /* same note */
        if (note == t->cand_note) t->cand_count++;
        else { t->cand_note = note; t->cand_count = 1; }
        /* a new note: twice in a row (or the first one); an octave jump, a common misreading, three times */
        if (t->cand_count >= ((note - t->note == 12 || t->note - note == 12) ? 3 : 2) || t->note < 0) {
            float cy;
            t->note = note; t->cand_count = 0;
            tuner_set_ref(t, note);
            cy = 1200.0f * log2f(f / t->ref);                 /* until the strobe has measured: the estimate */
            while (cy > 600.0f) cy -= 1200.0f;
            while (cy < -600.0f) cy += 1200.0f;
            t->cents = cy > 99.0f ? 99.0f : cy < -99.0f ? -99.0f : cy;
        }
    }
}

/* one block of GP5_BLOCK samples, full scale +-2^31 */
static void tuner_block(tuner_t *t, const int32_t *in, unsigned n)
{
    /* Three tight passes over the block, the state in local variables (registers) while they run: memory traffic
     * per sample was most of the cost. n is at most 64 (GP5_BLOCK). */
    const float a = t->lpa;                                   /* two one-pole low-passes for the phase measurement */
    float yb[64];
    uint8_t ix[64];
    unsigned i, k;
    if (n > 64u) n = 64u;
    {   /* pass 1: DC blocker, decimation for the pitch detector, the cents oscillator, the disc position */
        float dcx = t->dc_x, dcy = t->dc_y, lp1 = t->lp1, lp2 = t->lp2, acc = t->acc, sph = t->sph, sstep = t->sstep;
        float c = t->osc_c[0], sn = t->osc_s[0], rc = t->rot_c[0], rs = t->rot_s[0];
        float i1 = t->i1[0], q1 = t->q1[0], i2 = t->i2[0], q2 = t->q2[0];
        unsigned dec_n = t->dec_n, wpos = t->wpos;
        for (i = 0; i < n; i++) {
            float x = (float)in[i] * (1.0f / 2147483648.0f), y, c2;
            unsigned idx;
            y = x - dcx + 0.9995f * dcy; dcx = x; dcy = y;           /* DC blocker */
            yb[i] = y;
            /* the disc counts down, so that a sharp string's pattern moves up the segments (right on screen) */
            idx = (unsigned)(sph * (float)TN_SEG);
            if (idx >= TN_SEG) idx = TN_SEG - 1u;
            ix[i] = (uint8_t)(TN_SEG - 1u - idx);
            sph += sstep; if (sph >= 1.0f) sph -= 1.0f;
            i1 += a * (y * c - i1); q1 += a * (-y * sn - q1);         /* the fundamental against the note */
            i2 += a * (i1 - i2); q2 += a * (q1 - q2);
            c2 = c * rc - sn * rs; sn = sn * rc + c * rs; c = c2;
            lp1 += 0.157f * (y - lp1); lp2 += 0.157f * (lp1 - lp2);   /* ~1.2 kHz, twice, then 8-sample average */
            acc += lp2;
            if (++dec_n == TN_DEC) { t->hist[wpos++ & (TN_BUF - 1u)] = acc * (1.0f / (float)TN_DEC); acc = 0.0f; dec_n = 0; }
        }
        {   /* keep the oscillator on the unit circle */
            float g = 1.5f - 0.5f * (c * c + sn * sn);
            c *= g; sn *= g;
        }
        t->dc_x = dcx; t->dc_y = dcy; t->lp1 = lp1; t->lp2 = lp2; t->acc = acc; t->sph = sph;
        t->osc_c[0] = c; t->osc_s[0] = sn; t->i1[0] = i1; t->q1[0] = q1; t->i2[0] = i2; t->q2[0] = q2;
        t->dec_n = dec_n; t->wpos = wpos;
    }
    for (k = 0; k < TN_BANDS; k++) {   /* pass 2: per band, two band-passes in series; the lamp onto the disc */
        float bc = t->bc[k], br = t->br2[k], y1 = t->by1[k], y2 = t->by2[k];
        float cx1 = t->cx1[k], cx2 = t->cx2[k], cy1 = t->cy1[k], cy2 = t->cy2[k], xm1 = t->bx1, xm2 = t->bx2;
        uint16_t *on = t->on[k];
        for (i = 0; i < n; i++) {
            float x = yb[i], v = (x - xm2) + bc * y1 - br * y2, w;
            y2 = y1; y1 = v; xm2 = xm1; xm1 = x;
            w = (v - cx2) + bc * cy1 - br * cy2;
            cx2 = cx1; cx1 = v; cy2 = cy1; cy1 = w;
            on[ix[i]] += w > 0.0f;
        }
        t->by1[k] = y1; t->by2[k] = y2; t->cx1[k] = cx1; t->cx2[k] = cx2; t->cy1[k] = cy1; t->cy2[k] = cy2;
    }
    if (n >= 2u) { t->bx1 = yb[n - 1u]; t->bx2 = yb[n - 2u]; }
    for (i = 0; i < n; i++) t->cnt[ix[i]]++;               /* pass 3: how long each segment was lit for */
    {                                                         /* afterglow: segments the lamp passed over */
        unsigned g, b;
        for (g = 0; g < TN_SEG; g++) {
            if (!t->cnt[g]) continue;
            for (b = 0; b < TN_BANDS; b++) {
                float v = t->note >= 0 ? (float)t->on[b][g] / (float)t->cnt[g] : 0.0f;
                t->seg[b][g] += 0.35f * (v - t->seg[b][g]);
                t->on[b][g] = 0;
            }
            t->cnt[g] = 0;
        }
    }
    tuner_analyse(t);
    t->blk++;

    /* strobe positions, and cents from how fast the fundamental's row turns */
    {
        float rms = t->rms2 > 1e-20f ? sqrtf(t->rms2) : 1e-10f;
        for (k = 0; k < TN_ROWS; k++) {
            float ph = tn_atan2(t->q2[k], t->i2[k]) * (0.5f / TN_PI), m;
            if (ph < 0.0f) ph += 1.0f;
            m = 2.0f * sqrtf(t->i2[k] * t->i2[k] + t->q2[k] * t->q2[k]) / (rms * 1.41421356f);
            t->amp[k] = m > 1.0f ? 1.0f : m;
            if (k == 0 && t->note >= 0) {
                float dph = ph - t->last_ph[0];
                if (dph > 0.5f) dph -= 1.0f;
                if (dph < -0.5f) dph += 1.0f;
                if (t->settle < t->settle_n) t->settle++;     /* the low-pass settles after a new note */
                else { t->ph_acc += dph; t->ph_n += 1.0f; }
                if (t->ph_n >= (float)t->meas_n) {            /* cycles per block -> Hz -> cents */
                    float hz = t->ph_acc / t->ph_n * (TN_FS / (float)n);
                    float c = 1200.0f * log2f((t->ref + hz) / t->ref);
                    c = c > 99.0f ? 99.0f : c < -99.0f ? -99.0f : c;
                    t->cents += 0.5f * (c - t->cents);        /* a little smoothing: steadier figure */
                    t->ph_acc = 0.0f; t->ph_n = 0.0f;
                }
            }
            t->phase[k] = ph; t->last_ph[k] = ph;
        }
        if (t->note < 0) t->cents = 0.0f;
    }
}
#endif
