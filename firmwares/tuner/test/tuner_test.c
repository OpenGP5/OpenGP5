/* Tuner core on the computer (firmwares/tuner/test/run.sh): known tones in, what the tuner reports out.
 *   tuner_test tones          sines and plucked-string tones at set pitches and offsets: pass / fail
 *   tuner_test wav FILE.wav   a recording (16-bit or 24-bit PCM, first channel): note and cents every 0.1 s */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../dsp/tuner.h"

static tuner_t t;
static const char *NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/* a plucked string: harmonics 1..8 falling off, decaying; returns sample n */
static double tone(double f, double n, int pluck)
{
    double s = 0, tt = n / 44100.0;
    int h;
    if (!pluck) return sin(2 * M_PI * f * tt);
    for (h = 1; h <= 8; h++) s += sin(2 * M_PI * f * h * tt + h) / (h * 1.3) * exp(-tt * (0.6 + 0.4 * h));
    return s * 0.6;
}

static void strip(char *o, unsigned b)                      /* a band as text: # lit, + half, . dark */
{
    unsigned g;
    for (g = 0; g < TN_SEG; g++) o[g] = t.seg[b][g] > 0.66f ? '#' : t.seg[b][g] > 0.33f ? '+' : '.';
    o[TN_SEG] = 0;
}

/* how far band 0's pattern moved between two snapshots, in turns (-0.5 .. 0.5), by best circular match */
static double drift(const float *a, const float *b)
{
    double best = -1e30, bs = 0;
    int s, g;
    for (s = -(int)TN_SEG * 4; s < (int)TN_SEG * 4; s++) {          /* quarter-segment steps, interpolated */
        double c = 0, sh = s / 4.0;
        for (g = 0; g < (int)TN_SEG; g++) {
            double p = g - sh, f = floor(p); int i0 = (((int)f % (int)TN_SEG) + TN_SEG) % TN_SEG, i1 = (i0 + 1) % TN_SEG;
            double v = a[i0] + (a[i1] - a[i0]) * (p - f);
            c += v * b[g];
        }
        if (c > best) { best = c; bs = sh; }
    }
    bs /= TN_SEG;
    while (bs > 0.5) bs -= 1; while (bs < -0.5) bs += 1;
    return bs;
}

static int run_tone(double f, double cents_off, int pluck, double amp)
{
    int32_t blk[64];
    double fin = f * pow(2, cents_off / 1200), n = 0, moved, expect;
    unsigned b, i, nb = (unsigned)(1.5 * 44100 / 64), nb2 = (unsigned)(0.25 * 44100 / 64);
    int note_exp = (int)floor(69 + 12 * log2(f / 440) + 0.5), ok;
    float snap[TN_SEG];
    char s0[TN_SEG + 1], s2[TN_SEG + 1];
    tuner_init(&t, 440.0f);
    for (b = 0; b < nb + nb2; b++) {
        if (b == nb) memcpy(snap, t.seg[0], sizeof snap);
        for (i = 0; i < 64; i++, n++) blk[i] = (int32_t)(tone(fin, n, pluck) * amp * 2147483647.0);
        tuner_block(&t, blk, 64);
    }
    /* the pattern turns at (fin - ref) turns per second; over nb2 blocks: */
    expect = (fin - t.ref) * nb2 * 64 / 44100.0;
    while (expect > 0.5) expect -= 1; while (expect < -0.5) expect += 1;
    moved = drift(snap, t.seg[0]);
    ok = t.note == note_exp && fabs(t.cents - cents_off) <= (fabs(cents_off) > 20 ? 3.0 : 1.0)
         && (fabs(cents_off) > 10 || fabs(moved - expect) < 0.03);
    strip(s0, 0); strip(s2, 2);
    printf("%s %-6s %6.1f Hz %+6.1f c -> %-3s%d %+6.1f c  strobe moved %+.3f (expect %+.3f) |%s|%s|\n", ok ? "ok  " : "FAIL",
           pluck ? "pluck" : "sine", fin, cents_off,
           t.note >= 0 ? NAMES[t.note % 12] : "--", t.note >= 0 ? t.note / 12 - 1 : 0, t.cents, moved, expect, s0, s2);
    return ok;
}

static int wav(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char h[12], ch[8];
    unsigned nch = 1, bits = 16, rate = 44100, sz;
    int32_t blk[64];
    unsigned i = 0, b = 0;
    if (!f || fread(h, 1, 12, f) != 12) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    while (fread(ch, 1, 8, f) == 8) {
        sz = ch[4] | ch[5] << 8 | ch[6] << 16 | (unsigned)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            unsigned char fm[40]; fread(fm, 1, sz, f);
            nch = fm[2] | fm[3] << 8; rate = fm[4] | fm[5] << 8 | fm[6] << 16; bits = fm[14] | fm[15] << 8;
        } else if (!memcmp(ch, "data", 4)) break;
        else fseek(f, sz, SEEK_CUR);
    }
    printf("%s: %u Hz, %u bit, %u ch\n", path, rate, bits, nch);
    tuner_init(&t, 440.0f);
    for (;;) {
        unsigned char s[4 * 8];
        int32_t v;
        if (fread(s, bits / 8, nch, f) != nch) break;
        v = bits == 16 ? (int32_t)((uint32_t)(s[0] | s[1] << 8) << 16) : (int32_t)((uint32_t)(s[0] << 8 | s[1] << 16 | (unsigned)s[2] << 24));
        blk[i++] = v;
        if (i == 64) {
            tuner_block(&t, blk, 64); i = 0;
            if (getenv("TN_FROM") && b * 64 / 44100.0 >= atof(getenv("TN_FROM")) && b * 64 / 44100.0 < atof(getenv("TN_FROM")) + 0.12)
                printf("   blk %u note %d ref %.2f ph %.3f amp %.2f cents %+.1f acc %.3f n %.0f settle %u\n", b, t.note, t.ref,
                       t.phase[0], t.amp[0], t.cents, t.ph_acc, t.ph_n, t.settle);
            if (++b % 69 == 0)
                printf("%5.1f s  %5.1f dB  f %7.2f  %-3s%d %+6.1f c\n", b * 64 / 44100.0, t.level_db, t.freq,
                       t.note >= 0 ? NAMES[t.note % 12] : "--", t.note >= 0 ? t.note / 12 - 1 : 0, t.cents);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const double STR[] = { 30.87, 41.20, 55.0, 73.42, 98.0,                 /* 5-string bass */
                                  82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26 };   /* guitar, 12th fret E */
    static const double OFF[] = { 0, 3, -3, 10, -10, 30, -40 };
    unsigned s, o, pass = 0, all = 0;
    if (argc > 2 && !strcmp(argv[1], "wav")) return wav(argv[2]);
    for (s = 0; s < sizeof STR / sizeof STR[0]; s++)
        for (o = 0; o < sizeof OFF / sizeof OFF[0]; o++) {
            pass += run_tone(STR[s], OFF[o], 1, 0.5); all++;
            if (o < 3) { pass += run_tone(STR[s], OFF[o], 0, 0.05); all++; }
        }
    printf("%u of %u passed\n", pass, all);
    return pass == all ? 0 : 1;
}
