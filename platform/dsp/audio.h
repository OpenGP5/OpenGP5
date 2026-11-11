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
 * Sample rate 44.1 kHz (689 blocks of 64 per second, measured).
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
    REG(0x4000E01Cu) = 0x24F3200Fu;
    REG(0x4000E020u) = REG(0x4000E020u) | 0x8u;
    REG(0x4000E014u) = 0u;
    /* audio interface on (stock 0x76b8): config, prefill FIFOs with zeros, enable */
    REG(AIF + 0x04) = 0x7Fu;
    REG(AIF + 0x08) = 0x3FFFu;
    for (int k = 0; k < 8; k++)
        for (uint32_t r = 0x0C; r <= 0x40; r += 4) REG(AIF + r) = 0;
    REG(AIF + 0x00) = 0x8070741Bu;
}

#ifdef AUDIO_SPI1
/* SPI-1 <-> NDS32 block stream (stock: init 0x7c18/0x7d52, per block callback 0x572c -> 0x7da8).
 * 512 B each way per audio block via DMA0 (0x40020000) ch0 (SPI1 -> rx 0x1001fc00) and ch1
 * (tx 0x1001fe00 -> SPI1). Stock carries USB audio here; the NDS32 may wait on this stream. */
#define SPI1 0x4000C000u
#define DMA0 0x40020000u
#define SPI_RX 0x1001FC00u
#define SPI_TX 0x1001FE00u
static void spi1_init(void) {
    for (uint32_t a = SPI_RX; a < SPI_TX + 0x200u; a += 4) REG(a) = 0;
    REG(DMA0 + 0x108) = SPI1;   REG(DMA0 + 0x10C) = SPI_RX;
    REG(DMA0 + 0x100) = 0x800200C0u;          /* stock 0x800200c1 without arming */
    REG(DMA0 + 0x104) = 0x14D7u;
    REG(DMA0 + 0x228) = 0x23u;
    REG(DMA0 + 0x128) = SPI_TX; REG(DMA0 + 0x12C) = SPI1;
    REG(DMA0 + 0x120) = 0x80020090u;          /* stock 0x80020091 without arming */
    REG(DMA0 + 0x124) = 0x324Fu;
    REG(DMA0 + 0x224) = 0x22u;
    REG(DMA0 + 0x28) = 0xFFu;
    REG(DMA0 + 0x24) = 1u;
    REG(SPI1 + 0x04) = 0x000CC110u;
    REG(SPI1 + 0x10) = 0x000003EAu;
    REG(SPI1 + 0x08) = 0x00000200u;
    REG(SPI1 + 0x04) = 0x000CC110u;
}
static void spi1_block(void) {
    REG(SPI1 + 0x04) |= 0x1000u;                          /* stock 0x5768 */
    if (REG(DMA0 + 0x20) & 0x2u) { ST[22]++; return; }    /* previous transfer still busy */
    REG(SPI1 + 0x04) &= ~0x1000u;                         /* stock 0x7dfc */
    REG(SPI1 + 0x08) = 0x200u;
    REG(DMA0 + 0x114) = 0x200u;
    REG(DMA0 + 0x108) = SPI1;   REG(DMA0 + 0x10C) = SPI_RX;
    REG(DMA0 + 0x100) = 0x800200C1u;
    REG(DMA0 + 0x134) = 0x200u;
    REG(DMA0 + 0x128) = SPI_TX; REG(DMA0 + 0x12C) = SPI1;
    REG(DMA0 + 0x120) = 0x80020091u;
    REG(DMA0 + 0x28) = 0xFFu;                             /* clear done flags (polled design) */
    ST[21]++;
    uint32_t orv = 0;
    for (uint32_t a = SPI_RX; a < SPI_RX + 0x200u; a += 4) orv |= REG(a);
    if (orv) ST[23]++;                                    /* blocks with non-zero SPI rx data */
}
#endif

/* Poll: when all 4 channels finished a block, arm the other half and pass the finished input
 * half straight to the finished output half (it plays one block later). Returns 1 per block. */
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
        fw_process(blk, bl, br);
        for (int i = 0; i < 64; i++) {
            outL[i] = (uint32_t)(int32_t)(((int64_t)bl[i] * g) >> 16);
            outR[i] = (uint32_t)(int32_t)(((int64_t)br[i] * g) >> 16);
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
#ifdef AUDIO_SPI1
    spi1_block();
#endif
    uint32_t orv = 0;
    for (int i = 0; i < 64; i++) orv |= inL[i] | inR[i];
    ST[14] = inL[0];
    ST[15] = inR[0];
    if (orv) ST[20]++;            /* blocks with any non-zero input sample */
    return 1;
}
