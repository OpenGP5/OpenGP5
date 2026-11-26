/* BLE on the control processor: the RivieraWaves core set up by hand; connectable advertising; one connection as a
 * peripheral (milestone b). Our own code; addresses, bits, values, the order of the steps and the timing formulas
 * were read from MVsilicon's SDK library (Bt_init, rf_init, lld_init, em_buf_init, lld_adv_start, lld_evt_*,
 * lld_move_to_slave; work/bluetooth/notes.md). Nothing of it is copied or linked.
 *
 * Exchange memory (EM): the core sees it at 0x80000000 + offset; it is RAM 0x20048000.. (above everything the
 * platform links; the platform's protection unit maps 0x80000000 to it, uncached), set by 0x40030074 in KB.
 *
 * Timing: events run when the native clock reaches a slot (625 us) whose exchange-table entry (slot & 15) is armed;
 * the BT core's fine timer interrupts a few slots before each event so the entry can be armed in time. After each
 * event the BLE interrupt reports its end: received packets are handled, the next event is scheduled. */
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "radio.h"
#include "ble.h"

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))
#define BIT_SET(a, m, v) (R32(a) = (R32(a) & ~(uint32_t)(m)) | (uint32_t)(v))

#ifndef EM
#define EM        0x80000000u           /* a host test points this at an array */
#endif
#define EM16(off) R16(EM + (off))
#define EM8(off)  R8(EM + (off))
#define EM_KB     0x120u                 /* 0x20048000 */
#define EM_SIZE   0x1000u

#ifndef GP5_CTL_MHZ
#define GP5_CTL_MHZ 288u
#endif
extern volatile uint32_t gp5_link_t_in, gp5_link_t_out;   /* audio_link.c: its handler's last start / end (cycles) */
#define BLE       0x40008200u            /* BLE registers */
#define BT        0x40008000u            /* classic core registers (shared clock, fine timer, radio timing) */
#define MDM_B     0x4000B000u
#define MDM_C     0x4000C000u

/* EM layout (BLE) */
#define EM_EXTAB  0x000u                 /* exchange table, 16 x 4 B */
#define EM_FREQ   0x040u                 /* frequency table, 80 B */
#define CS(n)     (0x0B6u + 90u * (n))   /* control structures: 0 = the connection, 1 = advertising */
#define CS_CONN   CS(0)
#define CS_ADV    CS(1)
/* CS fields */
#define C_CNTL    0x00u
#define C_FCNTOFF 0x02u
#define C_LINK    0x04u
#define C_SYNCL   0x06u
#define C_SYNCH   0x08u
#define C_CRC0    0x0Au
#define C_CRC1    0x0Cu
#define C_FILT    0x0Eu
#define C_HOP     0x10u                  /* bit 15 hop on, 12..8 hop increment, 5..0 channel index */
#define C_TXRX    0x12u                  /* 7..0 TX power, bit 12 nesn */
#define C_RXWIN   0x14u                  /* bit 15: in slots, else us */
#define C_TXDESC  0x16u
#define C_MINEVT  0x18u
#define C_MAXEVT  0x1Au
#define C_CHMAP0  0x1Cu
#define C_CHMAP1  0x1Eu
#define C_CHMAP2  0x20u                  /* 4..0 channels 32..36, 15..8 number of used channels */
#define C_RXMAXB  0x22u
#define C_RXMAXT  0x24u
#define C_EVTCNT  0x4Au
#define C_SYNC0   0x4Eu                  /* the native clock (slots) at the last receive sync */
#define C_SYNC1   0x50u
#define C_FSYNC   0x52u                  /* 9..0 fine counter at that sync (counts down), bit 15 a packet was received */
#define C_PRIO    0x58u

#define TXD(n)    (0x22Au + 8u * (n))    /* TX descriptors: cntl (15 done, 14..0 next), header, data pointer, dle */
#define TXD_SCAN  2u
#define TXD_ADV   3u
#define TXD_DATA  4u                     /* 4, 5, 6: data packets of the connection */
#define TXBUF(n)  (0x39Au + 38u * (n))   /* small buffers of descriptors 0..3 */
#define TXDBUF(n) (0x432u + 0x104u * (n))
#define RXD(n)    (0x33Au + 12u * (n))   /* RX descriptors: cntl (15 done, next), status, header, chass, data pointer */
#define RXN       8u
#define RXBUF(n)  (0x73Eu + 0x104u * (n))

#define TXPWR     19u                    /* 0 dBm */
#define TRIM      0x18u                  /* crystal trim: the SDK's default (0x05..0x1D) */
#define OUR_PPM   50u                    /* our sleep clock accuracy, as assumed for window widening */
#define SLOT_MASK 0x07FFFFFFu
#define REQ_DELAY  60u                   /* us from the request's end to the event-end interrupt reading the time (a guess;
                                          * the first packet's offset from the window's middle is NOT this error: the
                                          * central may send anywhere in its transmit window) */
static uint32_t req_wait_us;              /* us rx_service waited for a CONNECT_IND's payload (conn_start) */
#define REQ_MARGIN 250u                  /* us more on each side of the first receive window (5000 didn't help quick
                                          * reconnects: they followed a stale request, see rx_service) */

struct ble_state ble;
struct ble_cfg ble_cfg = { 100, 0 };
/* Debug builds (GP5_BT_TRACE): a trace, a per-channel tally and the last refused request, in .noinit RAM (kept across
 * a watchdog restart, not a power cut; the old place, 0x80006C00..0x80007400 in Bluetooth's memory view, is only
 * backed when 0x4003007C = 0x10, which stops the audio link). ble_start logs where it is; the tools in
 * work/bluetooth/tools read that line. cht: [0] hop | map count << 8, [1..2] the channel map (5 bytes), [4 + u] for
 * unmapped channel u (0..36): hits | misses << 16. trace: word 0 the next index (bit 31: frozen), then 255 entries. */
#ifdef GP5_BT_TRACE
struct bt_debug { uint32_t trace[256], cht[48], refused[9]; };
volatile struct bt_debug bt_debug __attribute__((section(".noinit"), aligned(4)));
#define CHT (bt_debug.cht)
#else
volatile uint32_t bt_cht[48];             /* the same table in every build: cheap, and read by symbol (ble_channels.py) */
#define CHT bt_cht
#endif     /* receive window after a sync (us), window start offset (us) */

/* The trace (debug builds): code << 24 | value. It freezes when a connection ends, so what led up to it stays until
 * the next start. Read it with work/bluetooth/tools/bt_trace.py. */
#define TRACE (bt_debug.trace)
#define TRN   256u
#ifndef GP5_BT_TRACE                                           /* debug builds only (work/ble-conn) */
#define tr(code, v) ((void)0)
#else
static void tr(uint32_t code, uint32_t v)
{
    uint32_t i = TRACE[0];
    if (i & 0x80000000u) return;
    if (i < 1u || i >= TRN) i = 1u;
    TRACE[i] = code << 24 | (v & 0xFFFFFFu);
    TRACE[0] = i + 1u >= TRN ? 1u : i + 1u;
}
#endif

static uint8_t our_addr[6];

/* ------------------------------------------------------------------------------------------------ clock, set-up */

static uint32_t clock_halfslots(void)    /* the native clock, 312.5 us units */
{
    uint32_t k = 0;
    R32(BT + 0x1C) |= 1u << 31;
    while ((R32(BT + 0x1C) & (1u << 31)) && ++k < 10000u) { }
    return R32(BT + 0x1C) & 0x0FFFFFFFu;
}
static uint32_t slot_now(void) { return ((clock_halfslots() + 1u) >> 1) & SLOT_MASK; }
static int32_t slot_diff(uint32_t a, uint32_t b) { return (int32_t)((a - b) << 5) >> 5; }   /* a - b, 27-bit wrap */

static void em_clear(void) { uint32_t i; for (i = 0; i < EM_SIZE; i += 4u) R32(EM + i) = 0; }

static void system_on(void)              /* Bt_init's system writes (clocks, power, EM place, trim) */
{
#ifdef GP5_BT_MEM7C                                             /* Bt_init writes 0x10 here; the platform keeps 0. With
                                                                 * 0x10 the audio link's SPI/DMA stopped (Dreamcatcher,
                                                                 * 2026-11-23): left out while that is tested */
    R32(0x4003007Cu) = 0x10;
#endif
    R8(0x40021034u) |= 0x01;
    R32(0x40030070u) = 0x18;
    R32(0x40030078u) = 0x08;
    R32(0x40030074u) = EM_KB;
    R8(0x400300BCu) |= 0x03;
    BIT_SET(0x40021070u, 0x3FFu, (TRIM & 0x1Fu) | ((TRIM & 0x1Fu) << 5));
    R16(0x400300B0u) = (uint16_t)((R16(0x400300B0u) & ~0x180u) | 0x100u);
}

static int rf_setup(void)                /* rf_init: the cores' radio timing, the modem, the radio */
{
    unsigned i;
    int r;
    for (i = 0; i < 40u; i++) R8(EM + EM_FREQ + i) = (uint8_t)(2u * i);
    for (i = 0; i < 40u; i++) R8(EM + EM_FREQ + 40u + i) = (uint8_t)(2u * i + 1u);
    BIT_SET(BLE + 0x80, 0x7F000000u, 0x0Eu << 24);
    BIT_SET(BLE + 0x80, 0x00FF0000u, 0x46u << 16);
    BIT_SET(BLE + 0x80, 0x00001F00u, 0x0Fu << 8);
    BIT_SET(BLE + 0x80, 0x000000FFu, 0x46u);
    BIT_SET(BLE + 0x70, (1u << 23) | (1u << 22) | (1u << 21) | (3u << 5) | 1u, 0);
    R32(BLE + 0x74) = 0;
    BIT_SET(BLE + 0x78, 0x007F0000u, 0);
    BIT_SET(BLE + 0x78, 0x00000FFFu, 0);
    R32(BLE + 0x7C) = EM_FREQ;
    BIT_SET(BLE + 0x74, 0x001F0000u, 2u << 16);
    BIT_SET(BT + 0x28, 0x3FFu, 0x3FFu);
    BIT_SET(BT + 0x80, 0x00FF0000u, 0x40u << 16);
    BIT_SET(BT + 0x80, 0x00001F00u, 0x10u << 8);
    BIT_SET(BT + 0x80, 0x000000FFu, 0x40u);
    BIT_SET(BT + 0x80, 0x7F000000u, 0x0Eu << 24);
    BIT_SET(BT + 0x70, (1u << 23) | (1u << 22) | (3u << 5) | (1u << 21) | 1u, 0);
    R32(BT + 0x7C) = EM_FREQ;
    BIT_SET(BT + 0x78, 0x00000FFFu, 0);
    BIT_SET(BT + 0x78, 0x007F0000u, 0);
    BIT_SET(BT + 0x74, 0x001F0000u, 2u << 16);
    R32(BT + 0x74) &= ~0xFFFFu;
    BIT_SET(BLE + 0x70, 1u << 20, 0);
    BIT_SET(BLE + 0x190, 1u << 15, 1u << 15); BIT_SET(BLE + 0x190, 0xFFu, 0x20u);
    BIT_SET(BT + 0x1E0, 1u << 15, 1u << 15);  BIT_SET(BT + 0x1E0, 0xFFu, 0x20u);
    R32(MDM_B + 0x844) = 0x37;
    BIT_SET(MDM_C + 0x30, 0xFu, 0xAu);
    R32(MDM_B + 0x814) = 0x7854;
    r = radio_start();
    if (r) return r;
    BIT_SET(MDM_C + 0x1C, 0xFFu, 0x43u);
    R32(MDM_C + 0x38) = 0x0F; R32(MDM_C + 0x3C) = 0x2A; R32(MDM_C + 0x40) = 0x41;
    R32(MDM_C + 0x04) = 0x50C0;
    R32(0x400300C8u) |= 0x200u;
    R8(0x400300BCu) = (uint8_t)((R8(0x400300BCu) & ~3u) | 1u);
    R32(MDM_C + 0x20) = 0x550; R32(MDM_C + 0x24) = 0x12;
    R32(MDM_B + 0x80C) = 0x300A;
    BIT_SET(MDM_B + 0x810, 0x3Fu, 0); BIT_SET(MDM_B + 0x810, 0xF00u, 0);
    BIT_SET(MDM_C + 0x00, 0xFu, 4u);
    R16(0x400300B0u) = (uint16_t)((R16(0x400300B0u) & ~0x3E00u) | 0x1000u);
    R16(0x400300B0u) = (uint16_t)((R16(0x400300B0u) & ~0x180u) | 0x100u);
    return 0;
}

static void core_setup(void)             /* lld_init + em_buf_init */
{
    unsigned n;
    BIT_SET(BLE + 0x00, 0xF0u, 0xEu << 4);
    BIT_SET(BLE + 0xF0, 0x1FFu, 0xD2u);
    BIT_SET(BLE + 0xF0, 0x03FF0000u, 0x1B8u << 16);
    R32(BLE + 0x0C) = 0x33A;                                    /* interrupts: rx, event end, crypt, error, ... */
    BIT_SET(BLE + 0x00, (1u << 21) | (1u << 20), 0);
    R32(BLE + 0x24) = (uint32_t)our_addr[0] | our_addr[1] << 8 | our_addr[2] << 16 | (uint32_t)our_addr[3] << 24;
    R32(BLE + 0x28) = (uint32_t)our_addr[4] | our_addr[5] << 8 | 1u << 16;   /* a random (static) address */
    BIT_SET(BLE + 0x00, 1u << 9, 0);
    R32(BLE + 0x90) = 7;
    R32(BLE + 0xB0) = 0x16A; R32(BLE + 0xB4) = 0x17C; R32(BLE + 0x120) = 0x18E;
    R32(BLE + 0x2C) = RXD(0);
    for (n = 0; n < 4u; n++) { EM16(TXD(n) + 4) = (uint16_t)TXBUF(n); EM16(TXD(n) + 6) = 0; EM16(TXD(n)) = 0x8000; }
    for (n = 0; n < 3u; n++) { EM16(TXD(TXD_DATA + n) + 4) = (uint16_t)TXDBUF(n); EM16(TXD(TXD_DATA + n)) = 0x8000; }
    for (n = 7; n < 9u; n++) { EM16(TXD(n) + 4) = 0; EM16(TXD(n) + 6) = 0; EM16(TXD(n)) = 0x8000; }
    for (n = 0; n < RXN; n++) { EM16(RXD(n) + 8) = (uint16_t)RXBUF(n); EM16(RXD(n)) = (uint16_t)RXD((n + 1u) % RXN); }
    BIT_SET(BLE + 0xE0, 1u << 12, 0);
}

/* ------------------------------------------------------------------------------------------- event scheduling */

static struct { uint32_t slot; uint16_t cs; uint8_t pending, armed; } ev;

static void arm_now(void)                /* the exchange-table entry for ev.slot */
{
    uint32_t e = EM_EXTAB + 4u * (ev.slot & 15u);
    EM16(e + 2) = ev.cs;
    EM16(e) = 2u;                                               /* mode BLE, status ready */
    ev.armed = 1; ev.pending = 0;
}

static void fine_timer(uint32_t slot)    /* the BT core's fine timer: an interrupt at that slot */
{
    R32(BT + 0xB8) = slot & SLOT_MASK;
    R32(BT + 0x18) = 0x200;
    R32(BT + 0x0C) = 0x200;
}

/* runs the next event of control structure cs at slot (its fine offset already in the CS): 0, or -1 if too late */
static int schedule(uint32_t slot, uint16_t cs)
{
    int32_t d = slot_diff(slot, slot_now());
    tr(0x50 | (cs == CS_CONN), (uint32_t)d & 0xFFFFFFu);
    if (d < 2) { ble.late++; return -1; }
    if (d > 4096) { ble.late++; return -1; }                    /* nonsense (more than 2.5 s away) */
    ev.slot = slot; ev.cs = cs; ev.pending = 1; ev.armed = 0;
    if (d <= 12) arm_now(); else fine_timer(slot - 8u);
    return 0;
}

static void clear_entry(void) { if (ev.armed) EM16(EM_EXTAB + 4u * (ev.slot & 15u)) = 0; ev.armed = 0; }

/* ------------------------------------------------------------------------------------------------ advertising */

static uint8_t adv_data[31], adv_len, scan_data[31], scan_len;

static void adv_setup(void)             /* ADV_IND (connectable) with an empty scan response */
{
    unsigned i;
    EM16(CS_ADV + C_CNTL) = 0xF004;
    EM16(CS_ADV + C_LINK) = (uint16_t)(1u | 2u << 12);
    EM16(CS_ADV + C_SYNCL) = 0xBED6; EM16(CS_ADV + C_SYNCH) = 0x8E89;
    EM16(CS_ADV + C_CRC0) = 0x5555; EM16(CS_ADV + C_CRC1) = 0x0055;
    EM16(CS_ADV + C_FILT) = 0;
    EM16(CS_ADV + C_HOP) = 0x8027;
    EM16(CS_ADV + C_TXRX) = TXPWR;
    EM16(CS_ADV + C_RXWIN) = 0;
    EM16(CS_ADV + C_TXDESC) = (uint16_t)TXD(TXD_ADV);
    EM16(CS_ADV + C_MAXEVT) = 8;
    EM16(CS_ADV + C_RXMAXB) = 0; EM16(CS_ADV + C_RXMAXT) = 0;
    for (i = 0x4Eu; i <= 0x56u; i += 2u) EM16(CS_ADV + i) = 0;
    EM16(CS_ADV + C_PRIO) = 8;
    R32(BLE + 0xA0) = 0x5DC;
    for (i = 0; i < adv_len; i++) EM8(TXBUF(TXD_ADV) + i) = adv_data[i];
    EM16(TXD(TXD_ADV) + 2) = (uint16_t)(0u | 1u << 6 | (6u + adv_len) << 8);   /* ADV_IND, TxAdd random */
    EM16(TXD(TXD_ADV) + 4) = (uint16_t)TXBUF(TXD_ADV);
    for (i = 0; i < scan_len; i++) EM8(TXBUF(TXD_SCAN) + i) = scan_data[i];
    EM16(TXD(TXD_SCAN) + 2) = (uint16_t)(4u | 1u << 6 | (6u + scan_len) << 8);   /* SCAN_RSP */
    EM16(TXD(TXD_SCAN) + 4) = (uint16_t)TXBUF(TXD_SCAN);
    EM16(TXD(TXD_ADV)) = (uint16_t)TXD(TXD_SCAN);               /* advertising, then the scan response, in a loop */
    EM16(TXD(TXD_SCAN)) = (uint16_t)TXD(TXD_ADV);
}

static uint32_t adv_next;

static void adv_schedule(uint32_t after)
{
    adv_next = (after + 160u + (slot_now() & 7u)) & SLOT_MASK;  /* about every 100 ms */
    EM16(CS_ADV + C_FCNTOFF) = 0;
    EM16(CS_ADV + C_EVTCNT) = (uint16_t)ble.events;
    (void)schedule(adv_next, CS_ADV);
}

/* ------------------------------------------------------------------------------------------- the connection */

static const uint16_t sca_ppm[8] = { 500, 250, 150, 100, 75, 50, 30, 20 };   /* Bluetooth spec, SCA field */

static struct {
    uint32_t base;                       /* the anchor the next one is counted from: slot */
    uint16_t fine;                       /* ... and us into it */
    uint16_t interval;                   /* slots */
    uint16_t latency, timeout;           /* timeout in 10 ms */
    uint16_t ppm;                        /* both sides' accuracy */
    uint16_t evt;                        /* connection event counter (of the next event) */
    uint32_t since_rx;                   /* events since a packet was last received */
    uint16_t wait_us;                    /* how long conn_start's interrupt may have waited (us) */
    uint16_t upd_instant; uint8_t upd; uint8_t upd_winsize; uint16_t upd_winoffset, upd_interval, upd_latency, upd_timeout;
    uint16_t map_instant; uint8_t map_upd; uint8_t map[5];
    uint8_t rxi;                         /* next RX descriptor to look at */
    uint8_t version_sent;
    uint8_t got_rx;                      /* a good packet arrived in this event */
    uint8_t ever_rx;                     /* ... in any event of this connection */
    uint32_t since_sync;                 /* events since the last sync */
    uint32_t tw_us;                      /* a transmit window still to find (us; 0 once synced) */
    uint8_t unmapped, hop;               /* channel selection #1: the last unmapped channel, the hop (for the tally) */
} cn;

static void conn_end(int reason);
static void tx_reset(void);
/* the last connection attempt, for the log (ble_watchdog): what was asked, and what its first events heard */
static struct { uint16_t winsize, winoffset, interval, wait_us, req_wait; uint32_t tw_us, err0, none0, ok0, miss0;
                uint32_t err, none, ok, miss; uint16_t evt;
                uint32_t late0, late; uint8_t ran, synced, ch[6], fs[6]; } try_;
static uint32_t drift_us(uint32_t slots) { return (slots * 41u * cn.ppm) >> 16; }   /* lld_evt_drift_compute */
/* A transmit window to find (us): the central's window plus REQ_MARGIN each side, the margin cut so the whole stays
 * 1 ms shorter than the interval (slots), and never below 250 us (a Mac's 7.5 ms interval with a 3.75 ms window) */
static uint32_t first_window_us(uint32_t winsize, uint32_t interval_slots)
{
    uint32_t w = winsize * 1250u, room = interval_slots * 625u, m = REQ_MARGIN;
    if (w + 2u * m + 1000u > room) m = room > w + 1500u ? (room - w - 1000u) / 2u : 250u;
    if (m < 250u) m = 250u;
    return w + 2u * m;
}

/* the receive window: w us (capped at half the interval), written to the CS in the core's units (bit 15: slots) */
static void window_us(uint32_t w)
{
    uint16_t v;
    if (w < 14u) w = 14u;
    /* at most half the interval, once synced (a transmit window still open may be longer: 7.5 ms with a Mac) */
    if (!cn.tw_us && cn.interval && w > (uint32_t)cn.interval * 625u / 2u - 150u) w = (uint32_t)cn.interval * 625u / 2u - 150u;
    if (w < 0x2000u) v = (uint16_t)w;
    else v = (uint16_t)((((w + 624u) / 625u) & 0x7FFFu) | 0x8000u);
    EM16(CS_CONN + C_RXWIN) = v;
}

/* the window for the next event, from scratch (no compounding): the transmit window still open (until the first
 * sync after a connection or an update), the minimum after a sync, and the drift since the last sync, both sides */
static void window(uint32_t tw_us, uint32_t slots_since_sync) { window_us(tw_us + ble_cfg.win + 2u * drift_us(slots_since_sync)); }

/* moves the anchor on by n slots and places the next event's window (lld_evt_slave_time_compute) */
static uint32_t next_window(uint32_t n)
{
    uint32_t off = cn.fine + n * 625u, rx = EM16(CS_CONN + C_RXWIN), start;
    uint32_t anchor = (off / 625u + cn.base) & SLOT_MASK;
    uint16_t afine = (uint16_t)(off % 625u);
    if (rx & 0x8000u) off -= ((rx & 0x7FFFu) >> 1) * 625u; else off -= rx >> 1;
    off = (uint32_t)((int32_t)off - 33 + ble_cfg.offset);
    start = (off / 625u + cn.base) & SLOT_MASK;
    EM16(CS_CONN + C_FCNTOFF) = (uint16_t)(off % 625u);
    cn.base = anchor; cn.fine = afine;
    return start;
}

static unsigned map_count(const uint8_t *m)
{
    unsigned i, c = 0;
    for (i = 0; i < 37u; i++) if (m[i >> 3] & (1u << (i & 7u))) c++;
    return c;
}

static void set_map(const uint8_t *m)
{
    CHT[1] = (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24;   /* the map in use */
    CHT[2] = m[4]; CHT[3]++;                                                                     /* (and how often set) */
    EM16(CS_CONN + C_CHMAP0) = (uint16_t)(m[0] | m[1] << 8);
    EM16(CS_CONN + C_CHMAP1) = (uint16_t)(m[2] | m[3] << 8);
    EM16(CS_CONN + C_CHMAP2) = (uint16_t)((m[4] & 0x1Fu) | map_count(m) << 8);
}

static void skip_channel(void)           /* an event that didn't run: the core didn't hop, so we do */
{
    uint16_t h = EM16(CS_CONN + C_HOP);
    unsigned ch = ((h & 0x3Fu) + ((h >> 8) & 0x1Fu)) % 37u;
    EM16(CS_CONN + C_HOP) = (uint16_t)((h & ~0x3Fu) | ch);
}

static void conn_schedule(uint32_t start)
{
    int32_t d = slot_diff(start, slot_now());
    if (d > 4096) { tr(0x63, (uint32_t)d); conn_end(0x3E); return; }   /* a computation gone wrong */
    if (d < 3) {                                                /* passed (or nearly): skip to the next one ahead */
        uint32_t k = (uint32_t)(3 - d + (int32_t)cn.interval - 1) / cn.interval, j;
        tr(0x64, ((uint32_t)d & 0xFFFFu) | k << 16);
        for (j = 0; j < k; j++) { skip_channel(); cn.unmapped = (uint8_t)((cn.unmapped + cn.hop) % 37u); }
        cn.evt = (uint16_t)(cn.evt + k); cn.since_rx += k; ble.late += k;
        cn.since_sync += k;
        window(cn.tw_us, (cn.since_sync + 1u) * cn.interval);
        start = next_window(k * cn.interval);
    }
    EM16(CS_CONN + C_EVTCNT) = cn.evt;
    if (schedule(start, CS_CONN)) { tr(0x65, slot_diff(start, slot_now()) & 0xFFFFFFu); conn_end(0x3E); }
}

/* CONNECT_IND (its 34 bytes: InitA, AdvA, LLData) received in the advertising event */
static void conn_start(const uint8_t *p)
{
    uint32_t i, n, fsync = EM16(CS_ADV + C_FSYNC) & 0x3FFu, f, start;
    uint8_t winsize = p[19];
    uint16_t winoffset = (uint16_t)(p[20] | p[21] << 8), iv = (uint16_t)(p[22] | p[23] << 8);
    for (i = 0; i < 6u; i++) if (p[6 + i] != our_addr[i]) { ble.not_ours++; adv_schedule(slot_now()); return; }   /* AdvA */
    if (!(p[12] | p[13] | p[14] | p[15])) { adv_schedule(slot_now()); return; }   /* no access address: a SCAN_REQ (12 bytes)
                                                                 * read with a descriptor's stale header, not a request */
    {   /* every field within the spec's limits, or it's not a request we can follow: a descriptor read under load can
         * hold a mix of packets (2026-11-23: a "request" with latency 96 and a 328 s time-out was taken, and the
         * connection the Mac never joined stayed open for minutes, the pedal no longer advertising) */
        uint16_t lat = (uint16_t)(p[24] | p[25] << 8), to = (uint16_t)(p[26] | p[27] << 8);
        uint32_t aa = (uint32_t)p[12] | (uint32_t)p[13] << 8 | (uint32_t)p[14] << 16 | (uint32_t)p[15] << 24;
        unsigned hop = p[33] & 0x1Fu;
        if (lat > 499u || to < 10u || to > 3200u || (uint32_t)to * 10000u <= (1u + lat) * (uint32_t)iv * 2500u ||
            hop < 5u || hop > 16u || aa == 0x8E89BED6u || !aa || aa == 0xFFFFFFFFu) iv = 0;   /* (refused below) */
    }
    if (iv < 6u || iv > 3200u || winsize < 1u || winsize > 8u || winoffset > iv || map_count(p + 28) < 2u) {
#ifdef GP5_BT_TRACE
        for (i = 0; i < 34u; i++) ((volatile uint8_t *)bt_debug.refused)[i] = p[i];      /* the last refused request (debug) */
#endif
        ble.bad_req++; tr(0x33, iv); tr(0x34, (uint32_t)winsize << 16 | winoffset); tr(0x35, map_count(p + 28));
        adv_schedule(slot_now()); return;                       /* not a request we can follow */
    }
    EM16(CS_CONN + C_CNTL) = 3;                                  /* format: peripheral */
    EM16(CS_CONN + C_HOP) = (uint16_t)(0x8000u | (p[33] & 0x1Fu) << 8);
    EM16(CS_CONN + C_SYNCL) = (uint16_t)(p[12] | p[13] << 8); EM16(CS_CONN + C_SYNCH) = (uint16_t)(p[14] | p[15] << 8);
    EM16(CS_CONN + C_CRC0) = (uint16_t)(p[16] | p[17] << 8); EM16(CS_CONN + C_CRC1) = p[18];
    EM16(CS_CONN + C_TXRX) = TXPWR;
    EM16(CS_CONN + C_LINK) = (uint16_t)(0u | 2u << 12);         /* link label 0, rx threshold 2 */
    for (i = 0; i < 5u; i++) cn.map[i] = p[28 + i];
    set_map(cn.map);
    EM16(CS_CONN + C_FILT) = 0;
    for (i = 0x4Eu; i <= 0x56u; i += 2u) EM16(CS_CONN + i) = 0;
    EM16(CS_CONN + C_MINEVT) = 0; EM16(CS_CONN + C_MAXEVT) = 0;
    EM16(CS_CONN + C_RXMAXB) = 0; EM16(CS_CONN + C_RXMAXT) = 0;
    EM16(CS_CONN + C_TXDESC) = 0;
    EM16(CS_CONN + C_PRIO) = 8;
    cn.interval = (uint16_t)(2u * (p[22] | p[23] << 8));
    cn.latency = (uint16_t)(p[24] | p[25] << 8);
    cn.timeout = (uint16_t)(p[26] | p[27] << 8);
    cn.ppm = (uint16_t)(sca_ppm[p[33] >> 5] + OUR_PPM);
    cn.evt = 0; cn.since_rx = 0; cn.upd = 0; cn.map_upd = 0; cn.version_sent = 0; cn.ever_rx = 0;
    cn.got_rx = 0;                                               /* (the last connection's last packet set it) */
    /* Timing base: the request has just ended (this runs in the event-end interrupt). The core doesn't latch the
     * request's sync in the advertising CS here (its sync fields stay 0), so the base is the BLE time counter now,
     * less a little for the interrupt's delay; the first window gets REQ_MARGIN us more on each side. */
    {
        uint32_t k = 0, pos;
        R32(BLE + 0x1C) |= 1u << 31;                            /* sample the BLE time: slot and fine counter */
        while ((R32(BLE + 0x1C) & (1u << 31)) && ++k < 10000u) { }
        cn.base = R32(BLE + 0x1C) & SLOT_MASK;
        pos = 624u - (R32(BLE + 0x20) & 0x3FFu);                /* us into the slot (the counter counts down) */
        try_.req_wait = (uint16_t)req_wait_us;
        {   uint32_t back = REQ_DELAY + req_wait_us;                    /* us from the request's end to now */
            if (pos >= back) cn.fine = (uint16_t)(pos - back);
            else { uint32_t b = (back - pos + 624u) / 625u; cn.base = (cn.base - b) & SLOT_MASK; cn.fine = (uint16_t)(pos + b * 625u - back); }
            req_wait_us = 0;
        }
        (void)fsync; (void)f;
    }
    n = winsize + 2u + 2u * winoffset;
    {   /* This interrupt shares the audio link handler's level and may have waited behind it (NAM Loader: ~1.1 ms of
         * each 1.33 ms block), and the base above assumes it came at once: it is late by the wait, up to the handler's
         * run if this interrupt began just as the handler ended. The first window then moves half that earlier and
         * grows by it, so the central's first packet is in it however long the wait was (first_delta was -1.3 ms and
         * the connection never synced, 2026-11-23). */
        uint32_t t = __nds32__mfsr(NDS32_SR_PFMC0), wait = 0;
        if (t - gp5_link_t_out < GP5_CTL_MHZ * 40u && gp5_link_t_out - gp5_link_t_in < GP5_CTL_MHZ * 1500u)
            wait = (t - gp5_link_t_in) / GP5_CTL_MHZ;          /* us */
        if (wait > 1500u) wait = 1500u;
        cn.wait_us = (uint16_t)wait;
        tr(0x47, wait);
        if (wait / 2u <= cn.fine) cn.fine = (uint16_t)(cn.fine - wait / 2u);
        else { uint32_t b = (wait / 2u - cn.fine + 624u) / 625u; cn.base = (cn.base - b) & SLOT_MASK; cn.fine = (uint16_t)(cn.fine + b * 625u - wait / 2u); }
    }
    tr(0x40, n << 8 | winsize); tr(0x41, cn.interval); tr(0x42, cn.base); tr(0x43, cn.fine);
    cn.since_sync = 0; cn.tw_us = first_window_us(winsize, cn.interval) + cn.wait_us;
    try_.winsize = winsize; try_.winoffset = winoffset; try_.interval = iv; try_.wait_us = cn.wait_us; try_.tw_us = cn.tw_us;
    try_.err0 = ble.rx_err; try_.none0 = ble.rx_none; try_.ok0 = ble.rx_ok; try_.miss0 = ble.missed;
    try_.late0 = ble.late; try_.ran = 0; try_.synced = 0;
    for (i = 0; i < 6u; i++) { try_.ch[i] = 0xFF; try_.fs[i] = 0; }
    cn.unmapped = 0; cn.hop = (uint8_t)(p[33] & 0x1Fu);
    for (i = 0; i < 48u; i++) CHT[i] = 0;
    CHT[0] = cn.hop | map_count(cn.map) << 8;
    CHT[1] = (uint32_t)cn.map[0] | (uint32_t)cn.map[1] << 8 | (uint32_t)cn.map[2] << 16 | (uint32_t)cn.map[3] << 24;
    CHT[2] = cn.map[4];
    window(cn.tw_us, n);
    start = next_window(n);
    tr(0x44, start); tr(0x45, EM16(CS_CONN + C_RXWIN)); tr(0x46, slot_now());
    tx_reset();
    ble.state = BLE_CONNECTED; ble.connects++; ble.first_synced = 0;
    ble.interval = cn.interval;
    conn_schedule(start);
}

static void conn_end(int reason)
{
    tr(0x60, (uint32_t)reason);
    tr(0x61, ble.late); tr(0x62, cn.evt);
#ifdef GP5_BT_TRACE
    TRACE[0] |= 0x80000000u;                                     /* keep what led up to this */
#endif
    clear_entry();
    try_.err = ble.rx_err - try_.err0; try_.none = ble.rx_none - try_.none0; try_.ok = ble.rx_ok - try_.ok0;
    try_.miss = ble.missed - try_.miss0; try_.evt = cn.evt; try_.late = ble.late - try_.late0;
    ble.state = BLE_ADVERTISING; ble.disconnects++; ble.last_reason = (uint8_t)reason;
    EM16(CS_CONN + C_TXDESC) = 0;
    adv_schedule(slot_now());
}

/* --------------------------------------------------------------------------------------- sending data packets */

extern void gp5_irq_global(int on);
extern unsigned gp5_irq_global_state(void);
#define TXQ 32u                          /* a protocol reply is up to ~11 notifications */
static struct { uint8_t len, llid, data[27]; } txq[TXQ];
static uint8_t txq_head, txq_n;

static void send(uint8_t llid, const uint8_t *d, uint8_t len)
{
    unsigned i;
    unsigned g = gp5_irq_global_state();                         /* also called from the main loop */
    gp5_irq_global(0);
    if (txq_n >= TXQ || len > 27u) { ble.tx_drop++; if (g) gp5_irq_global(1); return; }
    i = (txq_head + txq_n) % TXQ;
    txq[i].llid = llid; txq[i].len = len;
    for (unsigned k = 0; k < len; k++) txq[i].data[k] = d[k];
    txq_n++;
    if (g) gp5_irq_global(1);
}

/* Up to TXF packets in flight, as a chain of TX descriptors (data: 4, 5, 6 with their buffers; empty: 7, 8): the core
 * sends them one after the other in an event (more data) and marks each done when the central acknowledged it.
 * Runs at an event's end, when the core is idle. The way MVsilicon's library does it (lld_pdu_tx_prog / _check):
 *  - done ones are taken off the front (and their next pointer cleared);
 *  - new ones are linked behind the last one, done or not;
 *  - a peripheral's chain always ends in an empty packet (LLID 1, length 0), so a data packet is never the last
 *    one (the core lets go of the chain after sending the last);
 *  - the CS's pointer is set to the first one not yet acknowledged, every time (pointing it at a new packet while
 *    an older one wasn't acknowledged skipped that one: 15d, the Mac couldn't connect). */
#define TXF 3u
#define TXD_EMPTY (TXD_DATA + TXF)       /* 7, 8 */
static uint8_t lst[TXF + 2u], lst_n;     /* the chain, in order: descriptor numbers */
static uint8_t data_busy, empty_busy;    /* bit per data slot / empty descriptor */

static void tx_reset(void)
{
    lst_n = 0; data_busy = empty_busy = 0; txq_head = txq_n = 0;
    EM16(CS_CONN + C_TXDESC) = 0;
}

static void chain_add(unsigned d)
{
    EM16(TXD(d)) = 0;                                           /* not done, no next */
    if (lst_n) { unsigned t = lst[lst_n - 1u]; EM16(TXD(t)) = (uint16_t)((EM16(TXD(t)) & 0x8000u) | TXD(d)); }
    lst[lst_n++] = (uint8_t)d;
}

static void tx_service(void)
{
    unsigned k, d;
    while (lst_n && (EM16(TXD(lst[0])) & 0x8000u)) {           /* acknowledged: free it */
        d = lst[0];
        EM16(TXD(d)) = 0x8000u;
        if (d >= TXD_EMPTY) empty_busy &= (uint8_t)~(1u << (d - TXD_EMPTY));
        else { data_busy &= (uint8_t)~(1u << (d - TXD_DATA)); ble.tx_ok++; }
        for (k = 1; k < lst_n; k++) lst[k - 1u] = lst[k];
        lst_n--;
    }
    while (txq_n && data_busy != (1u << TXF) - 1u) {
        unsigned slot = 0, i = txq_head;
        while (data_busy & (1u << slot)) slot++;
        d = TXD_DATA + slot;
        for (k = 0; k < txq[i].len; k++) EM8(TXDBUF(slot) + k) = txq[i].data[k];
        EM16(TXD(d) + 2) = (uint16_t)(txq[i].llid | txq[i].len << 8);
        EM16(TXD(d) + 4) = (uint16_t)TXDBUF(slot);
        EM16(TXD(d) + 6) = 0;
        data_busy |= (uint8_t)(1u << slot);
        chain_add(d);
        txq_head = (uint8_t)((txq_head + 1u) % TXQ); txq_n--;
    }
    if (lst_n && lst[lst_n - 1u] < TXD_EMPTY && empty_busy != 3u) {   /* the empty packet at the end */
        unsigned e = (empty_busy & 1u) ? 1u : 0u;
        d = TXD_EMPTY + e;
        EM16(TXD(d) + 2) = 1u;                                  /* LLID 1, length 0 */
        EM16(TXD(d) + 4) = 0;
        EM16(TXD(d) + 6) = 0;
        empty_busy |= (uint8_t)(1u << e);
        chain_add(d);
    }
    d = 0;
    for (k = 0; k < lst_n; k++) if (!(EM16(TXD(lst[k])) & 0x8000u)) { d = TXD(lst[k]); break; }
    EM16(CS_CONN + C_TXDESC) = (uint16_t)d;
}

#ifdef GP5_BT_TRACE
/* debug builds: the protocol messages (LL control in, ATT in, L2CAP out), the last 64, with the BLE slot clock:
 * [0] entries written, then [code << 24 | value] [slot] pairs. Read by symbol (work/usb-audio/ble_proto.py). */
volatile uint32_t bt_proto_log[1 + 128];
static void plog(uint32_t code, uint32_t v)
{
    uint32_t k = bt_proto_log[0] & 63u;
    bt_proto_log[1 + 2 * k] = code << 24 | (v & 0xFFFFFFu);
    bt_proto_log[2 + 2 * k] = slot_now();
    bt_proto_log[0]++;
}
#else
#define plog(c, v) ((void)0)
#endif

/* ------------------------------------------------------------------------------ LL control, L2CAP, ATT, SMP */

static void ll_ctrl(const uint8_t *p, unsigned len)
{
    uint8_t r[27];
    if (!len) return;
    ble.ll_rx++;
    plog(0x82, (uint32_t)p[0] << 16 | (len > 1 ? p[1] : 0) << 8 | len);   /* debug: LL control in: opcode, 2nd byte, length */
    switch (p[0]) {
    case 0x00:                                                  /* LL_CONNECTION_UPDATE_IND */
        if (len < 12u) return;
        cn.upd_winsize = p[1]; cn.upd_winoffset = (uint16_t)(p[2] | p[3] << 8);
        cn.upd_interval = (uint16_t)(2u * (p[4] | p[5] << 8)); cn.upd_latency = (uint16_t)(p[6] | p[7] << 8);
        cn.upd_timeout = (uint16_t)(p[8] | p[9] << 8); cn.upd_instant = (uint16_t)(p[10] | p[11] << 8);
        cn.upd = 1;
        return;
    case 0x01:                                                  /* LL_CHANNEL_MAP_IND */
        if (len < 8u) return;
        for (unsigned i = 0; i < 5u; i++) cn.map[i] = p[1 + i];
        cn.map_instant = (uint16_t)(p[6] | p[7] << 8); cn.map_upd = 1;
        return;
    case 0x02:                                                  /* LL_TERMINATE_IND */
        conn_end(len > 1 ? p[1] : 0);
        return;
    case 0x08:                                                  /* LL_FEATURE_REQ -> LL_FEATURE_RSP: none */
        r[0] = 0x09; for (unsigned i = 1; i < 9u; i++) r[i] = 0;
        send(3, r, 9);
        return;
    case 0x0C:                                                  /* LL_VERSION_IND */
        if (cn.version_sent) return;
        r[0] = 0x0C; r[1] = 0x06; r[2] = 0xFF; r[3] = 0xFF; r[4] = 0x01; r[5] = 0x00;   /* 4.0, company 0xFFFF */
        send(3, r, 6); cn.version_sent = 1;
        return;
    case 0x12:                                                  /* LL_PING_REQ */
        r[0] = 0x13; send(3, r, 1);
        return;
    case 0x0F:                                                  /* LL_CONNECTION_PARAM_REQ: rejected */
        r[0] = 0x11; r[1] = 0x0F; r[2] = 0x1A; send(3, r, 3);
        return;
    case 0x03:                                                  /* LL_ENC_REQ: no encryption */
        r[0] = 0x0D; r[1] = 0x1A; send(3, r, 2);
        return;
    case 0x07: case 0x09: case 0x0D: case 0x11: case 0x13: case 0x15:
        return;                                                 /* responses: nothing to answer */
    default:                                                    /* LL_UNKNOWN_RSP */
        r[0] = 0x07; r[1] = p[0]; send(3, r, 2);
        return;
    }
}

static void l2cap_send(uint16_t cid, const uint8_t *d, uint8_t len)
{
    plog(0x81, (uint32_t)cid << 16 | (uint32_t)d[0] << 8 | len);       /* debug: L2CAP out: channel, first byte, length */
    uint8_t r[27];
    r[0] = len; r[1] = 0; r[2] = (uint8_t)cid; r[3] = (uint8_t)(cid >> 8);
    for (unsigned i = 0; i < len && i < 23u; i++) r[4 + i] = d[i];
    send(2, r, (uint8_t)(4u + len));
}

/* ---- GATT server: GAP (device name) and the BLE-MIDI service (MIDI Manufacturers Association, BLE-MIDI 1.0) ---- */

static const uint8_t uuid_midi_svc[16] = { 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED,
                                           0x5A, 0x0E, 0xB8, 0x03 };   /* 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 */
static const uint8_t uuid_midi_chr[16] = { 0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38,
                                           0xDB, 0xE5, 0x72, 0x77 };   /* 7772E5DB-3868-4112-A1A9-F2669D106BF3 */
static const char dev_name[] = "OpenGP5";              /* "GP-5" before 1.0 */
static uint8_t cccd[2];                  /* the MIDI characteristic's notifications: bit 0 on */

enum { H_GAP = 1, H_NAME_DECL, H_NAME, H_APP_DECL, H_APP, H_GATT, H_MIDI, H_MIDI_DECL, H_MIDI_VAL, H_MIDI_CCCD, H_LAST = H_MIDI_CCCD };

struct attr { uint16_t type; const uint8_t *uuid128; const uint8_t *val; uint8_t len; };
static uint8_t decl_name[5] = { 0x02, H_NAME, 0, 0x00, 0x2A };
static uint8_t decl_app[5] = { 0x02, H_APP, 0, 0x01, 0x2A };
static uint8_t decl_midi[19] = { 0x16, H_MIDI_VAL, 0 };                  /* read, write without response, notify */
static const uint8_t svc_gap[2] = { 0x00, 0x18 }, svc_gatt[2] = { 0x01, 0x18 }, appearance[2] = { 0, 0 };
static const struct attr attrs[H_LAST + 1] = {
    [H_GAP]       = { 0x2800, 0, svc_gap, 2 },
    [H_NAME_DECL] = { 0x2803, 0, decl_name, 5 },
    [H_NAME]      = { 0x2A00, 0, (const uint8_t *)dev_name, sizeof dev_name - 1 },
    [H_APP_DECL]  = { 0x2803, 0, decl_app, 5 },
    [H_APP]       = { 0x2A01, 0, appearance, 2 },
    [H_GATT]      = { 0x2800, 0, svc_gatt, 2 },
    [H_MIDI]      = { 0x2800, 0, uuid_midi_svc, 16 },
    [H_MIDI_DECL] = { 0x2803, 0, decl_midi, 19 },
    [H_MIDI_VAL]  = { 0, uuid_midi_chr, 0, 0 },                  /* reads return nothing (BLE-MIDI) */
    [H_MIDI_CCCD] = { 0x2902, 0, cccd, 2 },
};
static const uint16_t group_end[H_LAST + 1] = { [H_GAP] = H_APP, [H_GATT] = H_GATT, [H_MIDI] = H_MIDI_CCCD };

void (*ble_midi_rx)(const uint8_t *bytes, unsigned n);        /* set by the firmware: MIDI bytes received */

static void att_err(uint8_t op, uint16_t h, uint8_t code)
{
    uint8_t r[5] = { 0x01, op, (uint8_t)h, (uint8_t)(h >> 8), code };
    l2cap_send(4, r, 5);
}

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* the type of handle h matches a 2- or 16-byte UUID */
static int type_is(unsigned h, const uint8_t *uuid, unsigned ulen)
{
    if (ulen == 2) return !attrs[h].uuid128 && attrs[h].type == u16(uuid);
    if (ulen == 16 && attrs[h].uuid128) { for (unsigned i = 0; i < 16u; i++) if (attrs[h].uuid128[i] != uuid[i]) return 0; return 1; }
    return 0;
}

/* ---- The platform's second MIDI port: SysEx from and to the central, as USB-MIDI-shaped events ---- */

#define PORTQ 256u                       /* events (4 bytes each) waiting for the main loop */
static uint8_t portq[PORTQ][4];
static volatile uint16_t portq_head, portq_n;
static uint8_t sx[3], sxn, in_sx;

static void port_push(uint8_t cin, uint8_t a, uint8_t b, uint8_t c)
{
    unsigned i;
    if (portq_n >= PORTQ) { ble.port_drop++; return; }
    i = (portq_head + portq_n) % PORTQ;
    portq[i][0] = cin; portq[i][1] = a; portq[i][2] = b; portq[i][3] = c;
    portq_n++;
}

static uint8_t ch_msg[3], ch_n, ch_need, ch_status;   /* a channel message being collected (running status) */

static void sysex_in(const uint8_t *b, unsigned n)     /* MIDI bytes, timestamps removed (runs in the interrupt) */
{
    unsigned i;
    for (i = 0; i < n; i++) {
        uint8_t v = b[i];
        if (v >= 0xF8u) { port_push(0x0F, v, 0, 0); continue; }   /* real-time (clock, start, stop): anywhere, even
                                                                 inside SysEx or between running-status bytes */
        if (!in_sx && v != 0xF0) {                          /* notes, controllers, program changes: for fw_midi() */
            if (v >= 0x80u && v < 0xF0u) { ch_status = v; ch_n = 0; ch_need = (v & 0xF0u) == 0xC0u || (v & 0xF0u) == 0xD0u ? 1u : 2u; continue; }
            if (v < 0x80u && ch_status) {
                ch_msg[ch_n++] = v;
                if (ch_n == ch_need) { port_push((uint8_t)(ch_status >> 4), ch_status, ch_msg[0], ch_need > 1u ? ch_msg[1] : 0); ch_n = 0; }
            }
            continue;
        }
        if (v == 0xF0) { in_sx = 1; sxn = 0; }
        if (!in_sx) continue;                               /* not SysEx */
        sx[sxn++] = v;
        if (v == 0xF7) { port_push((uint8_t)(0x04 + sxn), sx[0], sxn > 1 ? sx[1] : 0, sxn > 2 ? sx[2] : 0); sxn = 0; in_sx = 0; ble.sysex_rx++; }
        else if (sxn == 3u) { port_push(0x04, sx[0], sx[1], sx[2]); sxn = 0; }
    }
}

/* Main loop: a connection whose events stopped (nothing scheduled any more) ends, and advertising starts again, instead
 * of the pedal staying "connected" and invisible for good. Normal events come every 7.5..30 ms (12..48 slots). */
#define STALL_MS 200u
#define ADV_STALL_MS 500u                        /* advertising events come every ~100 ms */
extern uint32_t gp5_ms(void);                    /* hal/tick.c */
extern void gp5_log(const char *fmt, ...);
static void ble_watchdog(void)
{
    static uint32_t seen, since, log_c, log_d, log_a;
    uint32_t now = gp5_ms(), on;
    /* the log: connections coming and going, and why they ended (0x13: the other side left, 0x08: it went silent,
     * 0x3E: never established) */
    if (ble.connects != log_c) { log_c = ble.connects; gp5_log("bluetooth: connected (interval %u x 1.25 ms)", (unsigned)(ble.interval / 2u)); }
    if (ble.disconnects != log_d) {
        log_d = ble.disconnects;
        if (ble.last_reason == 0x3E || try_.evt <= 40u) {   /* failed, or ended soon after starting */
            gp5_log("bluetooth: ended (reason %02X) after %u events: asked window %u at %u, iv %u; waited %u+%u us, window %u us",
                    (unsigned)ble.last_reason, (unsigned)try_.evt, (unsigned)try_.winsize, (unsigned)try_.winoffset,
                    (unsigned)try_.interval, (unsigned)try_.req_wait, (unsigned)try_.wait_us, (unsigned)try_.tw_us);
            gp5_log("  ran %u, late %u; rx: %u empty, %u bad, %u good; missed %u", (unsigned)try_.ran, (unsigned)try_.late,
                    (unsigned)try_.none, (unsigned)try_.err, (unsigned)try_.ok, (unsigned)try_.miss);
            gp5_log("  events (ch/flags 1 ran 2 sync 4 rx): %u/%u %u/%u %u/%u %u/%u %u/%u %u/%u",
                    try_.ch[0], try_.fs[0], try_.ch[1], try_.fs[1], try_.ch[2], try_.fs[2],
                    try_.ch[3], try_.fs[3], try_.ch[4], try_.fs[4], try_.ch[5], try_.fs[5]);
        } else gp5_log("bluetooth: disconnected (reason %02X), advertising", (unsigned)ble.last_reason);
    }
    {   static uint32_t log_s;                   /* a connection's first packet: how far from the predicted anchor */
        if (ble.state == BLE_CONNECTED && ble.first_synced && log_s != ble.connects) {
            log_s = ble.connects;
            gp5_log("bluetooth: first packet %d us from the predicted anchor, event %u (asked window %u at %u; waited %u+%u us)",
                    (int)ble.first_delta, (unsigned)ble.first_evt, (unsigned)try_.winsize, (unsigned)try_.winoffset,
                    (unsigned)try_.req_wait, (unsigned)try_.wait_us);
            gp5_log("  events (ch/flags 1 ran 2 sync 4 rx): %u/%u %u/%u %u/%u; late %u", try_.ch[0], try_.fs[0],
                    try_.ch[1], try_.fs[1], try_.ch[2], try_.fs[2], (unsigned)(ble.late - try_.late0));
        }
    }
    if (ble.adv_restarts != log_a) { log_a = ble.adv_restarts; gp5_log("bluetooth: advertising had stopped; started again (%u)", (unsigned)ble.adv_restarts); }
    if (ble.events != seen || !since) { seen = ble.events; since = now ? now : 1u; return; }   /* (the first call: start
                                                                     timing, advertising may not have had an event yet) */
    if (ble.state == BLE_ADVERTISING) {
        /* advertising whose events stopped (an event end lost, say): the pedal invisible until a restart (seen
         * 2026-11-25, after a phone app's connection). Schedule it afresh. */
        if (now - since < ADV_STALL_MS) return;
        on = gp5_irq_global_state(); gp5_irq_global(0);
        if (ble.state == BLE_ADVERTISING && ble.events == seen) {
            ble.adv_restarts++;
            clear_entry(); ev.pending = 0; adv_schedule(slot_now());
        }
        if (on) gp5_irq_global(1);
        seen = ble.events; since = gp5_ms();
        return;
    }
    if (ble.state != BLE_CONNECTED) { since = now; return; }
    if (now - since < STALL_MS) return;
    on = gp5_irq_global_state(); gp5_irq_global(0);
    if (ble.state == BLE_CONNECTED && ble.events == seen) {
        ble.stalls++;
        tr(0x66, (uint32_t)ev.pending << 20 | (uint32_t)ev.armed << 16 | (ev.slot & 0xFFFFu));   /* what was waiting */
        tr(0x67, slot_now() & 0xFFFFFFu); tr(0x68, R32(BLE + 0x10) << 8 | (R32(BT + 0x10) & 0xFFu)); tr(0x69, R32(BT + 0xB8) & 0xFFFFFFu);
        clear_entry(); ev.pending = 0; conn_end(0x08);
    }
    if (on) gp5_irq_global(1);
    seen = ble.events; since = gp5_ms();
}

unsigned gp5_midi_port_read(uint8_t *ev, unsigned max)  /* the main loop takes the waiting events */
{
    ble_watchdog();
    unsigned k = 0, g = gp5_irq_global_state();
    gp5_irq_global(0);
    while (portq_n && k + 4u <= max) {
        ev[k] = portq[portq_head][0]; ev[k + 1] = portq[portq_head][1];
        ev[k + 2] = portq[portq_head][2]; ev[k + 3] = portq[portq_head][3];
        portq_head = (uint16_t)((portq_head + 1u) % PORTQ); portq_n--; k += 4u;
    }
    if (g) gp5_irq_global(1);
    return k;
}

/* the platform's reply (USB-MIDI SysEx events) -> BLE-MIDI notifications: [hdr][ts][F0..], [hdr][..], [hdr][..][ts][F7] */
static uint8_t ox[20], oxn;              /* the notification value being filled */
static int midi_notify(const uint8_t *v, unsigned n);

static void ox_flush(void) { if (oxn > 1u) (void)midi_notify(ox, oxn); oxn = 0; }

int gp5_midi_port_write(const uint8_t *ev, unsigned n)
{
    unsigned i, j, g;
    uint32_t ms;
    if (ble.state != BLE_CONNECTED || !ble.midi_notify) return 0;
    ms = (uint32_t)ble.clock * 5u / 8u;                         /* a timestamp in ms (the clock in slots) */
    g = gp5_irq_global_state(); gp5_irq_global(0);
    for (i = 0; i + 4u <= n; i += 4u) {
        unsigned cin = ev[i] & 0x0Fu, cnt = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : cin == 5u ? 1u : 0u;
        for (j = 0; j < cnt; j++) {
            uint8_t b = ev[i + 1 + j];
            if (!oxn) { ox[0] = (uint8_t)(0x80u | ((ms >> 7) & 0x3Fu)); oxn = 1; }
            if (b == 0xF0 || b == 0xF7) {                       /* both get a timestamp before them */
                if (oxn + 2u > sizeof ox) { ox_flush(); ox[0] = (uint8_t)(0x80u | ((ms >> 7) & 0x3Fu)); oxn = 1; }
                ox[oxn++] = (uint8_t)(0x80u | (ms & 0x7Fu));
            } else if (oxn + 1u > sizeof ox) { ox_flush(); ox[0] = (uint8_t)(0x80u | ((ms >> 7) & 0x3Fu)); oxn = 1; }
            ox[oxn++] = b;
            if (b == 0xF7) { ox_flush(); ble.sysex_tx++; }
        }
    }
    if (g) gp5_irq_global(1);
    return 1;
}

/* BLE-MIDI in: [header][timestamp][status][data...]([timestamp][status or running-status data...])...
 * A byte with bit 7 set is a timestamp, unless it directly follows one: then it is a status byte. */
static void midi_in(const uint8_t *p, unsigned n)
{
    uint8_t out[20];
    unsigned i, k = 0, after_ts = 0;
    if (n < 2u || !(p[0] & 0x80u)) return;
    for (i = 1; i < n; i++) {
        uint8_t b = p[i];
        if ((b & 0x80u) && !after_ts) { after_ts = 1; continue; }    /* a timestamp */
        after_ts = 0;
        if (k < sizeof out) out[k++] = b;
    }
    ble.midi_rx++;
    sysex_in(out, k);
    if (ble_midi_rx && k) ble_midi_rx(out, k);
}

static void att(const uint8_t *p, unsigned len)
{
    uint8_t r[23];
    unsigned h, k = 0, s, e;
    if (!len) return;
    ble.att_rx++;
    plog(0x80, (uint32_t)p[0] << 16 | (len > 2 ? (uint32_t)(p[1] | p[2] << 8) : 0u));   /* debug: ATT in: opcode, handle */
    s = len >= 3u ? u16(p + 1) : 0; e = len >= 5u ? u16(p + 3) : 0;
    switch (p[0]) {
    case 0x02:                                                  /* exchange MTU: ours stays 23 */
        r[0] = 0x03; r[1] = 23; r[2] = 0; l2cap_send(4, r, 3); return;
    case 0x04: {                                                /* find information */
        unsigned fmt = 0;
        if (!s || s > e) { att_err(p[0], (uint16_t)s, 0x01); return; }
        r[0] = 0x05; k = 2;
        for (h = s; h <= e && h <= H_LAST; h++) {
            unsigned f = attrs[h].uuid128 ? 2u : 1u;
            if (!fmt) fmt = f; else if (f != fmt) break;
            if (k + 2u + (f == 1u ? 2u : 16u) > 23u) break;
            r[k++] = (uint8_t)h; r[k++] = 0;
            if (f == 1u) { r[k++] = (uint8_t)attrs[h].type; r[k++] = (uint8_t)(attrs[h].type >> 8); }
            else for (unsigned i = 0; i < 16u; i++) r[k++] = attrs[h].uuid128[i];
        }
        if (!fmt) { att_err(p[0], (uint16_t)s, 0x0A); return; }
        r[1] = (uint8_t)fmt; l2cap_send(4, r, (uint8_t)k); return;
    }
    case 0x06:                                                  /* find by type value: primary service by UUID */
        if (len < 7u || u16(p + 5) != 0x2800) { att_err(p[0], (uint16_t)s, 0x0A); return; }
        r[0] = 0x07; k = 1;
        for (h = s; h <= e && h <= H_LAST; h++)
            if (!attrs[h].uuid128 && attrs[h].type == 0x2800 && attrs[h].len == len - 7u) {
                unsigned i;
                for (i = 0; i < attrs[h].len && attrs[h].val[i] == p[7 + i]; i++) { }
                if (i == attrs[h].len && k + 4u <= 23u) { r[k++] = (uint8_t)h; r[k++] = 0; r[k++] = (uint8_t)group_end[h]; r[k++] = 0; }
            }
        if (k == 1u) { att_err(p[0], (uint16_t)s, 0x0A); return; }
        l2cap_send(4, r, (uint8_t)k); return;
    case 0x08:                                                  /* read by type */
    case 0x10: {                                                /* read by group type (services) */
        unsigned ulen = len - 5u, each = 0;
        if (!s || s > e || (ulen != 2u && ulen != 16u)) { att_err(p[0], (uint16_t)s, 0x01); return; }
        if (p[0] == 0x10 && !(ulen == 2u && u16(p + 5) == 0x2800)) { att_err(p[0], (uint16_t)s, 0x10); return; }
        r[0] = (uint8_t)(p[0] + 1u); k = 2;
        for (h = s; h <= e && h <= H_LAST; h++) {
            unsigned n = attrs[h].len, size;
            if (!type_is(h, p + 5, ulen)) continue;
            if (n > (p[0] == 0x10 ? 16u : 19u)) n = p[0] == 0x10 ? 16u : 19u;
            size = 2u + (p[0] == 0x10 ? 2u : 0u) + n;
            if (!each) each = size; else if (size != each) break;
            if (k + size > 23u) break;
            r[k++] = (uint8_t)h; r[k++] = 0;
            if (p[0] == 0x10) { r[k++] = (uint8_t)group_end[h]; r[k++] = 0; }
            for (unsigned i = 0; i < n; i++) r[k++] = attrs[h].val[i];
        }
        if (!each) { att_err(p[0], (uint16_t)s, 0x0A); return; }
        r[1] = (uint8_t)each; l2cap_send(4, r, (uint8_t)k); return;
    }
    case 0x0A:                                                  /* read */
    case 0x0C: {                                                /* read blob */
        unsigned off = (p[0] == 0x0C && len >= 5u) ? u16(p + 3) : 0, n;
        if (!s || s > H_LAST) { att_err(p[0], (uint16_t)s, 0x01); return; }
        n = attrs[s].len > off ? attrs[s].len - off : 0;
        if (n > 22u) n = 22u;
        r[0] = (uint8_t)(p[0] + 1u);
        for (unsigned i = 0; i < n; i++) r[1 + i] = attrs[s].val[off + i];
        l2cap_send(4, r, (uint8_t)(1u + n)); return;
    }
    case 0x12:                                                  /* write request */
    case 0x52:                                                  /* write command */
        if (s == H_MIDI_CCCD && len >= 5u) { cccd[0] = p[3]; cccd[1] = p[4]; ble.midi_notify = cccd[0] & 1u; }
        else if (s == H_MIDI_VAL) midi_in(p + 3, len - 3u);
        else { if (p[0] == 0x12) att_err(p[0], (uint16_t)s, 0x03); return; }
        if (p[0] == 0x12) { r[0] = 0x13; l2cap_send(4, r, 1); }
        return;
    case 0x16: case 0x18: case 0x20:
        att_err(p[0], (uint16_t)s, 0x06); return;               /* not supported */
    default:
        return;                                                 /* commands, confirmations: nothing to answer */
    }
}

static int midi_notify(const uint8_t *v, unsigned n)   /* one notification of the MIDI characteristic */
{
    uint8_t r[23];
    unsigned i;
    if (ble.state != BLE_CONNECTED || !ble.midi_notify || n > 20u) return -1;
    r[0] = 0x1B; r[1] = H_MIDI_VAL; r[2] = 0;
    for (i = 0; i < n; i++) r[3 + i] = v[i];
    l2cap_send(4, r, (uint8_t)(3u + n));
    return 0;
}

/* sends MIDI bytes (one message, at most 18 bytes) to the connected central as a BLE-MIDI notification */
int ble_midi_send(const uint8_t *msg, unsigned n, uint32_t ms)
{
    uint8_t r[23];
    unsigned i;
    if (ble.state != BLE_CONNECTED || !ble.midi_notify || n > 18u) return -1;
    r[0] = 0x1B; r[1] = H_MIDI_VAL; r[2] = 0;                   /* handle value notification */
    r[3] = (uint8_t)(0x80u | ((ms >> 7) & 0x3Fu));              /* header: timestamp high */
    r[4] = (uint8_t)(0x80u | (ms & 0x7Fu));                     /* timestamp low */
    for (i = 0; i < n; i++) r[5 + i] = msg[i];
    l2cap_send(4, r, (uint8_t)(5u + n));
    ble.midi_tx++;
    return 0;
}

static void l2cap(const uint8_t *p, unsigned len)
{
    uint16_t plen, cid;
    uint8_t r[8];
    if (len < 4u) return;
    plen = (uint16_t)(p[0] | p[1] << 8); cid = (uint16_t)(p[2] | p[3] << 8);
    if (plen > len - 4u) return;                                /* fragmented: not with a 23-byte MTU */
    if (cid == 4) att(p + 4, plen);
    else if (cid == 6 && plen && p[4] == 0x01) { r[0] = 0x05; r[1] = 0x05; l2cap_send(6, r, 2); }   /* pairing: not supported */
    else if (cid == 5 && plen >= 4u && p[4] != 0x01 && p[4] != 0x13 && p[4] != 0x15) {
        r[0] = 0x01; r[1] = p[5]; r[2] = 2; r[3] = 0; r[4] = 0; r[5] = 0;                         /* command reject */
        l2cap_send(5, r, 6);
    }
}

/* ------------------------------------------------------------------------------------------------ interrupts */

static void rx_service(void)            /* every received packet since the last look */
{
    unsigned k;
    for (k = 0; k < RXN; k++) {
        uint16_t d = (uint16_t)RXD(cn.rxi), cntl = EM16(d), stat, ph, len, ptr;
        uint8_t buf[40];
        if (!(cntl & 0x8000u)) break;
        stat = EM16(d + 2); ph = EM16(d + 4); len = (uint16_t)(ph >> 8); ptr = EM16(d + 8);
        if (len > sizeof buf) len = sizeof buf;
        for (unsigned i = 0; i < len; i++) buf[i] = EM8(ptr + i);
        if (ble.state == BLE_ADVERTISING && (ph & 0xFu) == 5u && len >= 34u) {
            /* a CONNECT_IND: its payload may still be arriving in memory when this interrupt comes early (at priority
             * 2 its interval / access address read as zeros): wait for them, at most 300 us, and count the wait into
             * the connection's timing (conn_start) */
            uint32_t t0 = __nds32__mfsr(NDS32_SR_PFMC0), t = t0;
            while ((!(EM8(ptr + 22) | EM8(ptr + 23)) || !(EM8(ptr + 12) | EM8(ptr + 13) | EM8(ptr + 14) | EM8(ptr + 15))) &&
                   (t = __nds32__mfsr(NDS32_SR_PFMC0)) - t0 < GP5_CTL_MHZ * 300u) { }
            for (unsigned i = 0; i < len; i++) buf[i] = EM8(ptr + i);
            req_wait_us = (__nds32__mfsr(NDS32_SR_PFMC0) - t0) / GP5_CTL_MHZ;
        }
        /* free it, with the bytes the CONNECT_IND wait above looks at cleared: a buffer that last held a CONNECT_IND
         * still showed its access address and interval, the wait ended at once and the OLD request was followed (a
         * phone reconnecting within a second was never heard: 6 events, nothing; its "request" repeated the last
         * connection's window, offset and hop; 2026-11-26) */
        EM16(ptr + 12) = 0; EM16(ptr + 14) = 0; EM16(ptr + 22) = 0;   /* (buffers are 16-bit aligned; written as the core does) */
        EM16(d) = (uint16_t)(cntl & 0x7FFFu);
        cn.rxi = (uint8_t)((cn.rxi + 1u) % RXN);
        /* status: 1 type, 2 length, 3 CRC, 4 MIC errors = a bad packet; 5 SN error = a repeat of one already handled
         * (skip it); 6 NESN error only says the peer missed our last packet: its data is still new */
        if (stat & 0x1Eu) { ble.rx_err++; continue; }
        if ((stat & 0x20u) && ble.state == BLE_CONNECTED) { ble.rx_dup++; continue; }
        ble.rx_ok++;
        if (ble.state == BLE_CONNECTED && ((stat & 1u) || (ph & 3u) == 0u)) { ble.rx_none++; continue; }
            /* sync error: nothing was found in the window (the header left from the descriptor's last use) */
        if (ble.state == BLE_CONNECTED) { cn.got_rx = 1; cn.ever_rx = 1; }
        tr(0x30, (uint32_t)ph << 8 | (stat & 0xFFu));
        if (ble.state == BLE_ADVERTISING) {
            if ((ph & 0xFu) == 5u && len >= 34u) { ble.conn_req++; tr(0x31, 0); conn_start(buf); tr(0x32, ble.state); }
        } else if (len) {
            if ((ph & 3u) == 3u) ll_ctrl(buf, len);
            else if ((ph & 3u) == 2u) l2cap(buf, len);
        }
    }
}

static void conn_after_event(int ran)
{
    uint32_t n, start;
    tr(0x70 | (ran != 0), EM16(CS_CONN + C_FSYNC)); tr(0x72, EM16(CS_CONN + C_SYNC0)); tr(0x73, EM16(CS_CONN + C_RXWIN));
    if (cn.evt < 6u) {                           /* the log's record of a connection's first events (ble_watchdog) */
        try_.ch[cn.evt] = (uint8_t)(EM16(CS_CONN + C_HOP) & 0x3Fu);
        try_.fs[cn.evt] = (uint8_t)((ran ? 1u : 0u) | ((EM16(CS_CONN + C_FSYNC) & 0x8000u) ? 2u : 0u) | (cn.got_rx ? 4u : 0u));
        if (ran) try_.ran++;
    }
    if (!ran) skip_channel();
    cn.unmapped = (uint8_t)((cn.unmapped + cn.hop) % 37u);     /* the channel this event used (before remapping) */
    if (ran) CHT[4 + cn.unmapped] += (EM16(CS_CONN + C_FSYNC) & 0x8000u) ? 1u : 0x10000u;
    if (ran && (EM16(CS_CONN + C_FSYNC) & 0x8000u)) {          /* a packet arrived: re-base on it */
        if (cn.evt == 0u || !ble.first_synced) {                /* the first: how far from where we expected it */
            uint32_t sb = (uint32_t)EM16(CS_CONN + C_SYNC0) | (uint32_t)EM16(CS_CONN + C_SYNC1) << 16;
            int32_t us = slot_diff(sb, cn.base) * 625 + (int32_t)((624u - (EM16(CS_CONN + C_FSYNC) & 0x3FFu)) & 0xFFFFu) - (int32_t)cn.fine;
            ble.first_delta = us; ble.first_synced = 1; ble.first_evt = cn.evt;
        }
        {   /* how far the packet was from where we expected it (us; + = late) */
            uint32_t sb = (uint32_t)EM16(CS_CONN + C_SYNC0) | (uint32_t)EM16(CS_CONN + C_SYNC1) << 16;
            int32_t us = slot_diff(sb, cn.base) * 625 + (int32_t)((624u - (EM16(CS_CONN + C_FSYNC) & 0x3FFu)) & 0xFFFFu) - (int32_t)cn.fine;
            tr(0x74, (uint32_t)us & 0xFFFFFFu);
            ble.delta_sum += us; ble.delta_n++;
        }
        cn.base = (uint32_t)EM16(CS_CONN + C_SYNC0) | (uint32_t)EM16(CS_CONN + C_SYNC1) << 16;
        cn.fine = (uint16_t)((624u - (EM16(CS_CONN + C_FSYNC) & 0x3FFu)) & 0xFFFFu);
        cn.since_rx = 0; cn.since_sync = 0; cn.tw_us = 0;
        ble.synced++;
    } else if (ran && cn.got_rx) { cn.since_rx = 0; cn.since_sync++; ble.unsynced++; }   /* a packet, no timing */
    else { cn.since_rx++; cn.since_sync++; if (ran) ble.missed++; }
    cn.got_rx = 0;
    cn.evt++;
    if (!cn.ever_rx && cn.evt >= 6u) { conn_end(0x3E); return; }   /* no packet at all in the first 6 events: not established */
    if ((uint32_t)cn.since_rx * cn.interval * 625u >= (uint32_t)cn.timeout * 10000u) { conn_end(0x08); return; }
    if (cn.map_upd && cn.evt == cn.map_instant) { set_map(cn.map); cn.map_upd = 0; }
    if (cn.upd && cn.evt == cn.upd_instant) {                   /* connection update: a new transmit window */
        /* the new transmit window starts at the instant's old anchor + the offset (no extra 1.25 ms here, unlike after
         * CONNECT_IND: lld_evt_restart's winsize + 2 x winoffset + interval); n is to the window's middle */
        n = cn.interval + cn.upd_winsize + 2u * cn.upd_winoffset;
        cn.interval = cn.upd_interval; cn.latency = cn.upd_latency; cn.timeout = cn.upd_timeout; cn.upd = 0;
        ble.interval = cn.interval; ble.updates++;
        cn.tw_us = first_window_us(cn.upd_winsize, cn.upd_interval);   /* (slots already) */
        window(cn.tw_us, cn.since_sync * cn.interval + n);
    } else {
        n = cn.interval;
        window(cn.tw_us, (cn.since_sync + 1u) * n);
    }
    start = next_window(n);
    conn_schedule(start);
}

void gp5_ble_irq(void)                  /* BLE core: event end (bit 3), receive (bit 1), error (bit 5) */
{
    uint32_t st = 0, now, was = ble.state, fin = ev.slot, k = 0;
    /* Read and clear until nothing is left. Clearing only what one read saw lost an event end that came between the
     * read and the clear (the core's line stayed high, so the processor got no new edge): no more events were
     * scheduled and the connection stopped dead, the pedal still "connected" and no longer advertising. The USB
     * interrupt, above this one, widened that gap (USB audio streaming on NAM Loader, 2026-11-23). */
    while ((now = R32(BLE + 0x10)) != 0u && ++k < 8u) { R32(BLE + 0x18) = now; st |= now; }
    tr(0x10 | was, st);
    if (st & (1u << 5)) ble.errors++;
    if (st & (1u << 3)) { clear_entry(); ble.events++; }       /* the finished event's entry first */
    if (st & (1u << 3)) rx_service();                          /* at the event's end only (payloads complete) */
    if (!(st & (1u << 3))) return;
    if (was == BLE_CONNECTED && ble.state == BLE_CONNECTED) { tx_service(); conn_after_event(1); tx_service(); }
    else if (was == BLE_ADVERTISING && ble.state == BLE_ADVERTISING && !ev.pending) adv_schedule(fin);
}

void gp5_bt_irq(void)                   /* BT core: the fine timer, a few slots before an event */
{
    uint32_t st = 0, now, k = 0;
    while ((now = R32(BT + 0x10)) != 0u && ++k < 8u) { R32(BT + 0x18) = now; st |= now; }   /* (as gp5_ble_irq) */
    tr(0x20, st);
    if (!(st & 0x200u) || !ev.pending) return;
    if (slot_diff(ev.slot, slot_now()) < 2) {                  /* the timer came too late for it */
        ev.pending = 0; ble.late++;
        if (ble.state == BLE_CONNECTED) conn_after_event(0); else adv_schedule(slot_now());
        return;
    }
    arm_now();
}

/* --------------------------------------------------------------------------------------------------- start */

extern void gp5_irq_enable(unsigned line);
extern void gp5_irq_priority(unsigned line, unsigned priority);

int ble_start(const uint8_t addr[6], const uint8_t *adv, unsigned len, const uint8_t *scan, unsigned slen)
{
    int r;
    unsigned i;
#ifdef GP5_BT_TRACE
    for (i = 0; i < TRN; i++) TRACE[i] = 0;                    /* a new run: a new trace, no old crash record */
    TRACE[0] = 1;
#endif
    for (i = 0; i < 16u; i++) decl_midi[3 + i] = uuid_midi_chr[i];
    cccd[0] = cccd[1] = 0; ble.midi_notify = 0;
    for (i = 0; i < 6u; i++) our_addr[i] = addr[i];
    adv_len = (uint8_t)(len > 31u ? 31u : len);
    for (i = 0; i < adv_len; i++) adv_data[i] = adv[i];
    scan_len = (uint8_t)(slen > 31u ? 31u : slen);
    for (i = 0; i < scan_len; i++) scan_data[i] = scan[i];
    ble.state = BLE_OFF;
    em_clear();
    system_on();
    ble.version = R32(BLE + 0x04);
    r = rf_setup();
    ble.radio = r;
    if (r) return r;
    core_setup();
    adv_setup();
    cn.rxi = 0;
    R32(BLE + 0x18) = 0xFFFFFFFFu;
    R32(BT + 0x18) = 0xFFFFFFFFu;
    R32(BT + 0x0C) = 0;                                         /* classic core: only the fine timer, when used */
    BIT_SET(BLE + 0x00, 1u << 8, 1u << 8);                      /* rwble_en */
    /* Above the audio link's handler (3): behind a long on_block (NAM Loader ~80 % of each block) 50..90 % of the
     * connection events were missed; at 2, under 1 % (2026-11-23/24). What went wrong at 2 before is handled: a status
     * bit lost between read and clear (gp5_ble_irq reads until empty), and a request's payload read before it was
     * complete (rx_service waits for it, req_wait_us). */
    gp5_irq_priority(18, 2); gp5_irq_priority(19, 2);
    gp5_irq_enable(18); gp5_irq_enable(19);
    ble.state = BLE_ADVERTISING;
    adv_schedule(slot_now());
    return 0;
}

void ble_poll(void) { ble.clock = slot_now(); }
