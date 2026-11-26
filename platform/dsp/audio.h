/*
 * GP-5 DSP audio I/O, from scratch (polled, no interrupts). Included by responder.c when built
 * with -D AUDIO_PASSTHROUGH.
 *
 * Reversed from an emulator trace of the stock init (tools/dsp_init_trace.py) and the stock
 * re-arm code (region-c RAM 0x20001538 / 0x200015e2):
 *   audio interface  0x50000000: +0x0 ctrl (0x8070741b = on), +0x4 = 0x7f, +0x8 = 0x3fff,
 *                    +0x34/+0x38 input FIFOs (L/R?), +0x3c/+0x40 output FIFOs (L/R?)
 *   audio DMA        0x40030000: channel descriptors at +0x100 + 0x20*ch:
 *                    [+0 cfg | bit0 enable][+4 req cfg][+8 src][+0xc dst][+0x14 count words]
 *                    +0x0 any-pending, +0x4 done flags (bit ch), +0x8 write-1-to-clear,
 *                    +0x1c raw done (seen 0xf with +0x24 off), +0x24 irq enable (needed for +0/+4),
 *                    +0x28 = 0xff clear-all, +0x200 + 4*ch = 0x26 (request routing)
 *   ch0 (+0x100) out  0x1001f800 -> 0x5000003c   cfg 0x21291 req 0x2410
 *   ch1 (+0x120) out  0x1001fa00 -> 0x50000040   cfg 0x21291 req 0x2618
 *   ch2 (+0x140) in   0x50000034 -> 0x1001f400   cfg 0x212c1 req 0x0080
 *   ch3 (+0x160) in   0x50000038 -> 0x1001f600   cfg 0x212c1 req 0x0288
 *   Each channel owns 0x200 bytes = two 64-word halves (ping-pong). Stock arms the next half
 *   when all 4 channels report done, then processes the half that just completed.
 * Sample rate 44.1 kHz (689 blocks of 64 per second, measured), or 48 kHz when the firmware asks (GP5_SAMPLE_RATE):
 * stock picks the audio clock 0x4000e01c from a two-entry table at DSP 0xe7d0 (stock 0x7660, index in r2):
 * 0x24f3200f (used, 44.1 kHz) and 0x2029200f, the 48 kHz family. Nothing else differs.
 * Clocks: the resident already set the PLL (0x4000e054/58/5c). Stock adds the dividers
 * 0x4000e024..30 and the audio clock 0x4000e01c / 0x4000e020 bit 3 (both inside a
 * 0x4000e014 = 1 ... 0 write window), plus GPIO mux 0x40011020 / 0x40011058.
 */
#define AIF 0x50000000u
#define ADMA 0x40030000u
#define ABUF 0x1001F400u /* 4 channels x 0x200 bytes, code RAM (unused by this firmware) */

static const uint32_t ACH_DESC[4] = {ADMA + 0x140u, ADMA + 0x160u, ADMA + 0x100u, ADMA + 0x120u};
static const uint32_t ACH_FIFO[4] = {AIF + 0x34u, AIF + 0x38u, AIF + 0x3Cu, AIF + 0x40u};
static const uint32_t ACH_DONE[4] = {1u << 2, 1u << 3, 1u << 0, 1u << 1}; /* done bit = DMA channel index */
#define a_half ST[16] /* half currently armed (0/1) - in the status block: no .bss here */
#define a_done ST[17] /* done-flag accumulator */

#ifdef GP5_FIRMWARE
#include "gp5_dsp.h"          /* the firmware interface: fw_init / fw_process / fw_command */
#ifdef GP5_HAS_PARAMS
int32_t gp5_param_values[GP5_NPARAMS];
#endif
#endif

static void audio_arm(uint32_t half) {
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t d = ACH_DESC[i];
        uint32_t mem = ABUF + 0x200u * i + 0x100u * half;
        if (i < 2) { REG(d + 0x08) = ACH_FIFO[i]; REG(d + 0x0C) = mem; }   /* inputs: FIFO -> RAM */
        else       { REG(d + 0x08) = mem; REG(d + 0x0C) = ACH_FIFO[i]; }   /* outputs: RAM -> FIFO */
        REG(d + 0x14) = 64u;
        REG(d + 0x00) |= 1u;
    }
}

static void audio_init(void) {
#ifdef GP5_FIRMWARE
#ifdef GP5_HAS_PARAMS
    for (uint32_t k = 0; k < GP5_NPARAMS; k++) gp5_param_values[k] = gp5_param_table[k].def;   /* until the control processor's */
#endif
    fw_init();
    REG(0xE000EDFCu) |= 1u << 24;      /* DEMCR.TRCENA */
    REG(0xE0001004u) = 0;
    REG(0xE0001000u) |= 1u;            /* DWT cycle counter on */
#endif
    /* GPIO pad config (stock 0x5a54), 2 bits per pin. The resident's pre-boot init (0x6d0) leaves
     * +0x34 = 0x55557555; stock sets it back to 0x55555555 on every boot. Kept to match stock. It is
     * NOT what makes the input work: tested on hardware 2026-11-10, input stayed zero. */
    REG(0x40011030u) = 0x55555555u;
    REG(0x40011034u) = 0x55555555u;
    REG(0x4001106Cu) = 0x55555555u;
    /* GPIO mux (stock 0x5a54) */
    REG(0x40011020u) = 0x00444400u;
    REG(0x40011058u) = 0x00000011u;
    /* clock dividers (stock 0x8474). Stock changes single fields and KEEPS the other bits. On hardware
     * the registers end as +0x24 = 0x115, +0x28 = 0x1839db, +0x2c = 0x39db, +0x30 = 0x796079 (read under
     * stock 2026-11-10). Plain writes of 0x114 / 0xdb / 0xdb cleared the kept bits (0x183900, 0x3900,
     * bit 0): frame clock still ran, all converter data was zero. */
    REG(0x4000E028u) = (REG(0x4000E028u) & ~0x000040FFu) | 0xDBu;
    REG(0x4000E02Cu) = (REG(0x4000E02Cu) & ~0x000040FFu) | 0xDBu;
    REG(0x4000E030u) = (REG(0x4000E030u) & ~0x067FEF7Fu) | 0x00796079u;
    REG(0x4000E024u) = (REG(0x4000E024u) & ~0x0000237Eu) | 0x114u;
    /* zero both halves of all audio buffers */
    for (uint32_t a = ABUF; a < ABUF + 0x800u; a += 4) REG(a) = 0;
    /* audio DMA channel config (stock 0x77a4..0x77c6) */
    static const uint32_t cfg[4] = {0x212C1u, 0x212C1u, 0x21291u, 0x21291u};
    static const uint32_t req[4] = {0x0080u, 0x0288u, 0x2410u, 0x2618u};
    static const uint32_t route[4] = {0x200u, 0x204u, 0x208u, 0x20Cu};
    for (uint32_t i = 0; i < 4; i++) {
        REG(ACH_DESC[i] + 0x04) = req[i];
        REG(ACH_DESC[i] + 0x00) = cfg[i];
        REG(ADMA + route[i]) = 0x26u;
    }
    REG(ADMA + 0x28) = 0xFFu;
    REG(ADMA + 0x24) = 1u; /* DMA "irq enable": gates the done flags into +0x0/+0x4 (raw = +0x1c).
                            * NVIC IRQ33 stays disabled + PRIMASK set, so nothing is taken. */
    a_half = 0;
    a_done = 0;
    audio_arm(0);
    /* audio clock on (stock 0x7664) */
    REG(0x4000E014u) = 1u;
#if GP5_SAMPLE_RATE == 48000u
    REG(0x4000E01Cu) = 0x2029200Fu;    /* stock's table entry 1: 48 kHz */
#else
    REG(0x4000E01Cu) = 0x24F3200Fu;    /* entry 0: 44.1 kHz */
#endif
    REG(0x4000E020u) = REG(0x4000E020u) | 0x8u;
    REG(0x4000E014u) = 0u;
    /* audio interface on (stock 0x76b8): config, prefill FIFOs with zeros, enable */
    REG(AIF + 0x04) = 0x7Fu;
    REG(AIF + 0x08) = 0x3FFFu;
    for (int k = 0; k < 8; k++)
        for (uint32_t r = 0x0C; r <= 0x40; r += 4) REG(AIF + r) = 0;
    REG(AIF + 0x00) = 0x8070741Bu;
}

/* 1 once the control processor announced a hand-over to its loader (link command 0x7F05): the firmware's processing
 * and the audio link stop, so that the main loop answers the serial link promptly (the loader talks to the DSP, and
 * the DSP reads its serial port only between blocks). Plain passthrough until the next start. */
static uint32_t dsp_quiet;

#ifdef AUDIO_SPI1
/* SPI-1 <-> NDS32 block stream (stock: init 0x7c18/0x7d52, per block callback 0x572c -> 0x7da8).
 * One exchange each way per audio block via DMA0 (0x40020000) ch0 (SPI1 -> RAW_RX) and ch1 (RAW_TX -> SPI1);
 * stock used 512 B at 0x1001fc00 / 0x1001fe00 for USB audio. Ours carries the audio link (framing below). */
#define SPI1 0x4000C000u
#define DMA0 0x40020000u
/* Link framing. The control processor's SPI slave keeps a few bytes queued from one exchange to the next (its first
 * bytes out are the previous exchange's last ones; how many depends on start-up timing: 1 and 5 measured on the
 * pedal, 2026-11-13). So each side sends [sync word][GP5_LINK_WORDS words][LINK_PAD zero bytes], and the receiver
 * looks for the other side's sync word in the first LINK_PAD + 1 byte positions and copies the words after it into
 * a buffer of its own. The copy also keeps the words steady while the next exchange is already being received. */
#define LINK_SYNC_D 0xC0DE5A17u          /* DSP -> control processor */
#define LINK_SYNC_N 0xC0DE5A71u          /* control processor -> DSP */
#define LINK_PAD    32u
#ifndef GP5_LINK_WORDS                   /* a firmware may ask for more (build.py passes its #define) */
#define GP5_LINK_WORDS 128u
#endif
/* The words in one exchange: the firmware's GP5_LINK_WORDS, plus USB audio's 64 (manifest.py: GP5_LINK_TOTAL, with
 * GP5_USB_PLAY_AT / GP5_USB_REC_AT where they are). A firmware without the link of its own gets one for USB audio. */
#ifdef GP5_LINK_TOTAL
#define LINK_T      GP5_LINK_TOTAL
#else
#define LINK_T      GP5_LINK_WORDS
#endif
#define LINK_N      (4u + LINK_T * 4u + LINK_PAD)   /* bytes per exchange (0x224 for 128 words) */
#if LINK_T > 128u
/* More words than fit the platform's 3 KB at 0x1001E800: the four buffers go into the large block instead. */
#define LINK_NW     ((LINK_N + 31u) / 32u * 8u)
static uint32_t link_buf[2u * LINK_NW + 2u * LINK_T] __attribute__((section(".large"), aligned(32)));
#define RAW_RX      ((uint32_t)&link_buf[0])
#define LINK_IN     ((uint32_t)&link_buf[LINK_NW])
#define RAW_TX      ((uint32_t)&link_buf[LINK_NW + LINK_T])
#define LINK_OUT_AT ((uint32_t)&link_buf[2u * LINK_NW + LINK_T])
#else
#define RAW_RX      0x1001E800u          /* as received */
#define LINK_IN     0x1001EA40u          /* the words after the sync word, for fw_process() */
#define RAW_TX      0x1001EC40u          /* sync word, the firmware's words, padding */
#define LINK_OUT_AT 0x1001EE80u
#endif
#define SPI_RX      LINK_IN
static uint32_t link_lag = 0xFFFFFFFFu;  /* bytes before the sync word in the last exchange (0xFFFFFFFF = none) */
static void spi1_init(void) {
    for (uint32_t a = RAW_RX; a < RAW_TX + LINK_N; a += 4) REG(a) = 0;
    REG(RAW_TX) = LINK_SYNC_D;
    REG(DMA0 + 0x108) = SPI1;   REG(DMA0 + 0x10C) = RAW_RX;
    REG(DMA0 + 0x100) = 0x800200C0u;          /* stock 0x800200c1 without arming */
    REG(DMA0 + 0x104) = 0x14D7u;
    REG(DMA0 + 0x228) = 0x23u;
    REG(DMA0 + 0x128) = RAW_TX; REG(DMA0 + 0x12C) = SPI1;
    REG(DMA0 + 0x120) = 0x80020090u;          /* stock 0x80020091 without arming */
    REG(DMA0 + 0x124) = 0x324Fu;
    REG(DMA0 + 0x224) = 0x22u;
    REG(DMA0 + 0x28) = 0xFFu;
    REG(DMA0 + 0x24) = 1u;
    REG(SPI1 + 0x04) = 0x000CC110u;
    REG(SPI1 + 0x10) = 0x000003EAu;
    REG(SPI1 + 0x08) = LINK_N;
    REG(SPI1 + 0x04) = 0x000CC110u;
}
/* The firmware's view (gp5_dsp.h, GP5_AUDIO_LINK): during fw_process() gp5_link_in() holds the words the control
 * processor sent in the exchange started after the previous block (zeros if its sync word was not found), and
 * gp5_link_out() is free; spi1_block() starts the next exchange right after fw_process(). */
__attribute__((unused)) static const int32_t *gp5_link_in(void) { return (const int32_t *)LINK_IN; }
#ifdef GP5_LINK_EARLY
/* Early exchange (firmware.h: #define GP5_LINK_EARLY): one exchange takes about 105,000 cycles (38 % of a block,
 * measured 2026-11-14), so one started after a long fw_process() is still running when the next block begins and
 * the firmware would get the previous exchange's words again (heard as clicks). Here the exchange starts at the
 * beginning of the block instead and has the whole block to finish. The firmware writes its words into a buffer
 * of its own (gp5_link_out()), which goes out at the start of the next block; fw_link_early(), which the firmware
 * defines, may put words that are ready at once (the new input) straight into the exchange about to start. */
#define LINK_OUT    LINK_OUT_AT
__attribute__((unused)) static int32_t *gp5_link_out(void) { return (int32_t *)LINK_OUT; }
static void fw_link_early(const int32_t *in, int32_t *out);
#else
__attribute__((unused)) static int32_t *gp5_link_out(void) { return (int32_t *)(RAW_TX + 4u); }
#endif
static void spi1_take(void) {                             /* before fw_process(): find the sync word, copy */
    const volatile uint8_t *r = (const volatile uint8_t *)RAW_RX;
    volatile uint8_t *d = (volatile uint8_t *)LINK_IN;
    uint32_t o;
    for (o = 0; o <= LINK_PAD; o++)
        if (r[o] == (uint8_t)LINK_SYNC_N && r[o + 1] == (uint8_t)(LINK_SYNC_N >> 8) &&
            r[o + 2] == (uint8_t)(LINK_SYNC_N >> 16) && r[o + 3] == (uint8_t)(LINK_SYNC_N >> 24)) break;
    if (o > LINK_PAD) {
        for (uint32_t i = 0; i < LINK_T; i++) REG(LINK_IN + 4u * i) = 0;
        link_lag = 0xFFFFFFFFu; ST[23]++;                 /* exchanges without the sync word */
        return;
    }
    link_lag = o;
    if (!(o & 3u)) for (uint32_t i = 0; i < LINK_T; i++) REG(LINK_IN + 4u * i) = REG(RAW_RX + o + 4u + 4u * i);
    else for (uint32_t i = 0; i < LINK_T * 4u; i++) d[i] = r[o + 4u + i];
}
/* Before a hand-over (0x7F05): SPI-1 back to the state of a firmware without the link (control 0x21000, as measured)
 * and its two DMA channels off, so the wires to the control processor are as the factory loader expects them. */
static void spi1_quiet(void) {
    REG(DMA0 + 0x100) &= ~1u;
    REG(DMA0 + 0x120) &= ~1u;
    REG(DMA0 + 0x28) = 0xFFu;
    REG(SPI1 + 0x04) = 0x00021000u;
}
static void spi1_block(void) {
    if (dsp_quiet) return;
    REG(SPI1 + 0x04) |= 0x1000u;                          /* stock 0x5768 */
    if (REG(DMA0 + 0x20) & 0x2u) { ST[22]++; return; }    /* previous transfer still busy */
    REG(SPI1 + 0x04) &= ~0x1000u;                         /* stock 0x7dfc */
    REG(SPI1 + 0x08) = LINK_N;
    REG(DMA0 + 0x114) = LINK_N;
    REG(DMA0 + 0x108) = SPI1;   REG(DMA0 + 0x10C) = RAW_RX;
    REG(DMA0 + 0x100) = 0x800200C1u;
    REG(DMA0 + 0x134) = LINK_N;
    REG(DMA0 + 0x128) = RAW_TX; REG(DMA0 + 0x12C) = SPI1;
    REG(DMA0 + 0x120) = 0x80020091u;
    REG(DMA0 + 0x28) = 0xFFu;                             /* clear done flags (polled design) */
    ST[21]++;
}
#endif

#if defined(GP5_USB_AUDIO) && defined(AUDIO_SPI1) && defined(GP5_FIRMWARE)
/* USB audio (OpenGP5 1.0): the platform's 64 words of the link, one stereo 16-bit frame each (left in the low half),
 * as on the USB wire. Recording (unless the firmware records on the control processor): the firmware's finished
 * output, before the computer's playback is mixed in (no echo in a recording) and before the output level (the
 * volume knob is for listening). Playback: the computer's sound from the control processor, added to the outputs
 * before the output level, saturated. Full scale is +-2^31 on this side. */
/* The pedal's own sound at the outputs (command 0x7F06, from the control processor): off while the computer plays
 * it back (a DAW monitoring its input would otherwise be heard twice). Recording is unchanged. Glides over a block. */
static volatile uint8_t usb_direct_off;
static int32_t usb_direct_g = 65536;                           /* Q16, the level applied */
static void usb_audio_block(int32_t *bl, int32_t *br) {
    const volatile uint32_t *play = (const volatile uint32_t *)(LINK_IN + 4u * GP5_USB_PLAY_AT);
#ifndef GP5_USB_RECORD_CONTROL
    int32_t *rec = gp5_link_out() + GP5_USB_REC_AT;
    for (int i = 0; i < 64; i++)
        rec[i] = (int32_t)(((uint32_t)bl[i] >> 16) | ((uint32_t)br[i] & 0xFFFF0000u));
#endif
    {
        int32_t g0 = usb_direct_g, d = (usb_direct_off ? 0 : 65536) - g0;
        if (g0 != 65536 || d) {
            for (int i = 0; i < 64; i++) {
                int32_t gi = g0 + (d * (i + 1)) / 64;
                bl[i] = (int32_t)(((int64_t)bl[i] * gi) >> 16);
                br[i] = (int32_t)(((int64_t)br[i] * gi) >> 16);
            }
            usb_direct_g = g0 + d;
        }
    }
    for (int i = 0; i < 64; i++) {
        uint32_t w = play[i];
        int64_t l = (int64_t)bl[i] + (int64_t)(int32_t)(w << 16), r = (int64_t)br[i] + (int64_t)(int32_t)(w & 0xFFFF0000u);
        bl[i] = l > 0x7FFFFFFF ? 0x7FFFFFFF : l < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : (int32_t)l;
        br[i] = r > 0x7FFFFFFF ? 0x7FFFFFFF : r < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : (int32_t)r;
    }
}
#endif

/* Poll: when all 4 channels finished a block, arm the other half and pass the finished input
 * half straight to the finished output half (it plays one block later). Returns 1 per block. */
#define LEVEL_GLIDE 4096                                       /* Q16 level per block */
static int audio_poll(void) {
    ST[18] = REG(ADMA + 0x1C);
    if (!REG(ADMA + 0x00)) return 0;
    uint32_t f = REG(ADMA + 0x04) & 0xFu;
    if (!f) return 0;
    REG(ADMA + 0x08) = f;
    a_done |= f;
    if (a_done != 0xFu) return 0;
    a_done = 0;
    uint32_t fin = a_half;
    a_half = fin ^ 1u;
    audio_arm(fin ^ 1u);
    volatile uint32_t *inL = (volatile uint32_t *)(ABUF + 0x000u + 0x100u * fin);
    volatile uint32_t *inR = (volatile uint32_t *)(ABUF + 0x200u + 0x100u * fin);
    volatile uint32_t *outL = (volatile uint32_t *)(ABUF + 0x400u + 0x100u * fin);
    volatile uint32_t *outR = (volatile uint32_t *)(ABUF + 0x600u + 0x100u * fin);
#ifdef AUDIO_TONE
    /* test tone: about 405 Hz square, +/-0x08000000 (~ -24 dBFS if 32-bit left-justified), both outs */
    for (int i = 0; i < 64; i++) {
        uint32_t ph = ST[19]++ % 109u;                 /* period 109 samples: 405 Hz at 44.1 kHz */
        uint32_t v = ph < 55u ? 0x08000000u : 0xF8000000u;
        outL[i] = v; outR[i] = v;
    }
#elif defined(AUDIO_LEVEL)
    /* As stock's main mode: guitar = inA/2 - inB/2 (differential input), the same mono sample to both
     * outputs, times the output level the NDS32 sent (Q16, 0 until it sends one). */
    {
        int32_t g = (int32_t)ST[24];
        uint32_t peak = ST[26];
#ifdef GP5_FIRMWARE
        int32_t blk[64], bl[64], br[64];
        uint32_t t0 = REG(0xE0001004u);                        /* DWT cycle counter */
        for (int i = 0; i < 64; i++) {
            int32_t m = ((int32_t)inL[i] >> 1) - ((int32_t)inR[i] >> 1);
            uint32_t a = m < 0 ? (uint32_t)-m : (uint32_t)m;
            if (a > peak) peak = a;
            blk[i] = m;
        }
        if (dsp_quiet) {                                       /* before an update: plain passthrough */
            for (int i = 0; i < 64; i++) { bl[i] = blk[i]; br[i] = blk[i]; }
        } else {
#ifdef AUDIO_SPI1
            spi1_take();
#ifdef GP5_LINK_EARLY
            fw_link_early(blk, (int32_t *)LINK_OUT);
            for (uint32_t i = 0; i < LINK_T; i++) REG(RAW_TX + 4u + 4u * i) = REG(LINK_OUT + 4u * i);
            spi1_block();
#endif
#endif
            fw_process(blk, bl, br);
#if defined(GP5_USB_AUDIO) && defined(AUDIO_SPI1)
            usb_audio_block(bl, br);
#endif
        }
        /* The level glides to the one the NDS32 sent, at most LEVEL_GLIDE per block (full scale in 16 blocks, 23 ms):
         * the sound fades in when it comes on after start-up, and a change makes no click. */
        {
            static int32_t g_now;                              /* the level applied (0 at start: muted) */
            int32_t d = g - g_now;
            if (d > LEVEL_GLIDE) d = LEVEL_GLIDE; else if (d < -LEVEL_GLIDE) d = -LEVEL_GLIDE;
            for (int i = 0; i < 64; i++) {
                int32_t gi = g_now + (d * (i + 1)) / 64;
                outL[i] = (uint32_t)(int32_t)(((int64_t)bl[i] * gi) >> 16);
                outR[i] = (uint32_t)(int32_t)(((int64_t)br[i] * gi) >> 16);
            }
            g_now += d;
        }
        uint32_t dt = REG(0xE0001004u) - t0;
        ST[29] = dt;                                           /* core cycles for this block */
        if (dt > ST[30]) ST[30] = dt;                          /* worst block since boot */
        if (REG(ADMA + 0x04) & 0xFu) ST[31]++;                 /* next block already done: too slow */
#else
        for (int i = 0; i < 64; i++) {
            int32_t m = ((int32_t)inL[i] >> 1) - ((int32_t)inR[i] >> 1);
            uint32_t a = m < 0 ? (uint32_t)-m : (uint32_t)m;
            if (a > peak) peak = a;
            m = (int32_t)(((int64_t)m * g) >> 16);
            outL[i] = (uint32_t)m; outR[i] = (uint32_t)m;
        }
#endif
        ST[26] = peak;
    }
#else
    for (int i = 0; i < 64; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
#endif
    ST[13]++;
#if defined(AUDIO_SPI1) && !defined(GP5_LINK_EARLY)
    spi1_block();
#endif
    uint32_t orv = 0;
    for (int i = 0; i < 64; i++) orv |= inL[i] | inR[i];
    ST[14] = inL[0];
    ST[15] = inR[0];
    if (orv) ST[20]++;            /* blocks with any non-zero input sample */
    return 1;
}
