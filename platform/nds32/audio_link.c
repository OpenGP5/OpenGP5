/* GP-5 open platform: the audio link between the DSP and the control processor.
 *
 * The DSP is the SPI master: after each audio block it starts one exchange of GP5_LINK_WORDS 32-bit words each way
 * (SPI 1 there; the SPI slave + DMA channels 4 / 5 here, hal/spis.c). When an exchange has finished, the DMA
 * interrupt re-arms the next one at once with the words the firmware filled last time, then hands the words just
 * received to the firmware's on_block(), which fills the words for the exchange after that. Two buffers each way.
 * RAM is cached on this core and the DMA does not see the cache: received words are invalidated before they are
 * read, words to send are written back before the DMA reads them. */
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "gp5_firmware.h"
#include "hal/spis.h"
#include "start/irq.h"

/* Framing (the DSP side, platform/dsp/audio.h, has the same): each side sends [sync word][GP5_LINK_WORDS words]
 * [PAD zero bytes]. This side's SPI slave keeps a few bytes queued from one exchange to the next, so the DSP finds
 * the sync word a few bytes in and copies from there; here the DSP's words are looked for the same way. */
#define SYNC_D 0xC0DE5A17u                         /* DSP -> here */
#define SYNC_N 0xC0DE5A71u                         /* here -> DSP */
#define PAD    32u
#define BYTES  (4u + GP5_LINK_WORDS * 4u + PAD)
#define BUFW   (((BYTES + 31u) / 32u) * 8u + 8u)    /* whole cache lines, + slack for the search */
static int32_t rx[2][BUFW] __attribute__((aligned(32)));
static int32_t tx[2][BUFW] __attribute__((aligned(32)));
static int32_t in_copy[GP5_LINK_WORDS];
static unsigned cur, waiting, started;
static void (*on_block)(const int32_t *in, int32_t *out);
volatile uint32_t gp5_link_blocks, gp5_link_early, gp5_link_failed, gp5_link_lost, gp5_link_lag;

static const int32_t *find_words(const int32_t *raw)       /* the words after the DSP's sync word, or 0 */
{
    const uint8_t *r = (const uint8_t *)raw;
    uint8_t *d = (uint8_t *)in_copy;
    unsigned o, i;
    for (o = 0; o <= PAD; o++)
        if (r[o] == (uint8_t)SYNC_D && r[o + 1] == (uint8_t)(SYNC_D >> 8) &&
            r[o + 2] == (uint8_t)(SYNC_D >> 16) && r[o + 3] == (uint8_t)(SYNC_D >> 24)) break;
    if (o > PAD) return 0;
    gp5_link_lag = o;
    if (!(o & 3u)) return raw + o / 4u + 1u;
    for (i = 0; i < GP5_LINK_WORDS * 4u; i++) d[i] = r[o + 4u + i];
    return in_copy;
}

/* the cache operation must be a constant: one loop per kind */
#define LINES(p, op) do { uint32_t a_; \
    for (a_ = (uint32_t)(p); a_ < (uint32_t)(p) + BYTES; a_ += 32u) __nds32__cctlva_wbinval_one_lvl(op, (void *)a_); \
    __nds32__dsb(); } while (0)

void gp5_link_irq(void)
{
    unsigned k = cur;
    if (!gp5_spis_done()) {                                   /* receive done before send: comes again */
        gp5_link_early++;
        /* never finishing would keep this interrupt firing and starve everything else (USB too): give up */
        if (++waiting > 100000u) { gp5_irq_disable(GP5_IRQ_SPIS_DMA); gp5_link_failed = 1; }
        return;
    }
    waiting = 0;
    cur ^= 1u;
    gp5_spis_rearm(tx[cur], rx[cur], BYTES);
    LINES(rx[k], NDS32_CCTL_L1D_VA_INVAL);
    if (on_block) {
        const int32_t *in = find_words(rx[k]);
        unsigned i;
        if (!in) { for (i = 0; i < GP5_LINK_WORDS; i++) in_copy[i] = 0; in = in_copy; gp5_link_lost++; }
        on_block(in, tx[k] + 1);
    }
    LINES(tx[k], NDS32_CCTL_L1D_VA_WB);
    gp5_link_blocks++;
}

void gp5_link_start(void (*fn)(const int32_t *in, int32_t *out))
{
    unsigned i;
    on_block = fn;
    for (i = 0; i < BUFW; i++) { rx[0][i] = rx[1][i] = 0; tx[0][i] = tx[1][i] = 0; }
    tx[0][0] = tx[1][0] = (int32_t)SYNC_N;
    LINES(rx[0], NDS32_CCTL_L1D_VA_WBINVAL); LINES(rx[1], NDS32_CCTL_L1D_VA_WBINVAL);
    LINES(tx[0], NDS32_CCTL_L1D_VA_WB); LINES(tx[1], NDS32_CCTL_L1D_VA_WB);
    cur = 0; started = 1;
    gp5_spis_init();
    gp5_irq_priority(GP5_IRQ_SPIS_DMA, 0);
    gp5_spis_start(tx[0], rx[0], BYTES);
    gp5_irq_enable(GP5_IRQ_SPIS_DMA);
}

/* Before the hand-over to the factory loader (main.c): no more link interrupts, the slave stopped, and pins
 * A20..A23 back as they were (the loader needs A22). Harmless when the link never started. */
void gp5_link_stop(void)
{
    gp5_irq_disable(GP5_IRQ_SPIS_DMA);
    gp5_spis_release();
}

int gp5_link_running(void) { return started; }
