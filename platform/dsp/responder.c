/*
 * GP-5 DSP link responder: the platform's DSP side of the link to the control processor (frames, status
 * ACKs, the periodic 0x4001 report, output level, the firmware's own commands from 0x7F10).
 * Research builds with -D STOCK_REPLIES also answer Valeton's app's start-up queries with replies recorded
 * from Valeton's DSP firmware (tools/dsp_stock/, not part of the platform).
 *
 * Link: UART0 @0x40005000, 460800 8N1, polled. Frame: [crc8][len u16 LE][0x01][payload],
 * crc8 = CRC-8 poly 0x07 over payload. Payload starts with u16 cmd = (group << 12) | index.
 *
 * Reply policy, from a survey of all 31 stock region-c handlers (dispatcher 0x5c68):
 *   data replies: (6,0) version, (2,2)/(7,2) 76-byte module info, (6,2) two floats,
 *                 (5,2) 16 bytes, (7,5) 16 bytes then ACK. Values here are zeros/placeholders.
 *   no reply:     (0,2) (4,1) (5,0) (7,4)
 *                 ((1,1) was in this list from the handler survey; on hardware the stock DSP ACKs it:
 *                 our NDS32 FW 8 gets 7 ACKs for its 7 sync commands, two of them 0x1001. 2026-11-09)
 *   every other command: status ACK  01 04 00 14   (stock 0x7b84, code 0x14 = 20)
 * Hardware so far: v1 answered only (6,0); the NDS32 accepted it, stayed in normal mode (0184),
 * and next polled (2,2) id 8 forever (black screen).
 *
 * RAM (read over J7; SWD stays enabled in this firmware):
 *   0x20007F00: [0] "GP5!" [1] heartbeat [2] boots [3] fault [4] log entries [5] log full
 *               [6] frames ok [7] data replies [8] crc errors [9] acks sent [10] silent
 *               [11] fallback ACKs (cmd/arg not in the stock table) [12] 0x4001 reports sent
 *               [13] audio blocks [14]/[15] last in L/R word [16] armed half [17] done accum [18] DMA raw status +0x1c [19] tone phase [20] non-zero input blocks
 *               [21] SPI-1 blocks armed [22] SPI-1 busy skips [23] non-zero SPI rx blocks
 *               [24] output gain, Q16 (AUDIO_LEVEL) [25] output-level commands taken [26] peak |mono| since boot
 *               [27] resident tick patch state [28] patch runs (RESIDENT_TICK_PATCH, tickpatch.h)
 *               [29] core cycles for the last audio block [30] worst block [31] late blocks (GP5_FIRMWARE)
 *   0x20000E00..0x20005BFF variables, 0x10000000..0x1001E7FF GP5_LARGE buffers (link.ld, gp5_dsp.h)
 *   0x20006E00: tick patch context, 0x20006E80: its RAM stub; 0x20000000..0x20000DC4 holds the resident's
 *               RAM image while the patch runs (nothing else of ours lives there).
 *   0x20007000: frame log, 8 bytes/entry: [cmd u16][repeat count u16][first 4 arg bytes],
 *               consecutive identical frames merged; 480 entries max. args = payload bytes 4..7.
 *   0x20006C00: 512-byte receive buffer.
 */
#include <stdint.h>

#define GP5_PLATFORM_VERSION 0x00010000u   /* 1.0: answered to the version query (the screen shows V00010000) */
#define REG(a) (*(volatile uint32_t *)(a))
#define ST ((volatile uint32_t *)0x20007F00u)
#define LOGW ((volatile uint32_t *)0x20007000u)
#define LOG_MAX 480u
#define MAGIC 0x21355047u
#define UART0 0x40005000u
#define UART_CLK 96000000u
#define BAUD 460800u
#define DSP_VERSION 0x01010006u /* "V106" */

void resp_main(void) __attribute__((noreturn, used));

/* No C library: GCC lowers struct copies and zero-init to these calls. volatile pointers stop GCC
 * from turning the loops back into calls to themselves. */
void *memset(void *d, int c, unsigned int n) {
    volatile uint8_t *p = d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned int n) {
    volatile uint8_t *p = d; const volatile uint8_t *q = s;
    while (n--) *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, unsigned int n) {
    volatile uint8_t *p = d; const volatile uint8_t *q = s;
    if (p <= q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
void default_handler(void) __attribute__((noreturn));

/* Start-up: stack, floating-point unit on (full access for both privilege levels), then the C variables:
 * .data copied from flash, .bss and the GP5_LARGE buffers zeroed (link.ld). */
extern uint32_t __data_start[], __data_end[], __data_load[], __bss_start[], __bss_end[], __large_start[], __large_end[];
static void __attribute__((noreturn, used)) c_start(void) {
    uint32_t *d = __data_start;
    const uint32_t *s = __data_load;
    while (d < __data_end) *d++ = *s++;
    for (d = __bss_start; d < __bss_end; ) *d++ = 0;
    for (d = __large_start; d < __large_end; ) *d++ = 0;
    resp_main();
}

__attribute__((naked, noreturn)) void reset_handler(void) {
    __asm volatile("ldr r0, =0x20006C00\n"  /* stack below the rx buffer; status block stays clear */
                   "msr msp, r0\n"
                   "ldr r0, =0xE000ED88\n"  /* CPACR: CP10 + CP11 full access = the FPU */
                   "ldr r1, [r0]\n"
                   "orr r1, r1, #(0xF << 20)\n"
                   "str r1, [r0]\n"
                   "dsb\n"
                   "isb\n"
                   "b c_start\n");
}

static uint8_t crc8(const uint8_t *p, uint32_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (uint8_t)((c & 0x80u) ? ((uint32_t)c << 1) ^ 0x07u : ((uint32_t)c << 1));
    }
    return c;
}

static void uart_put(uint8_t b) {
    while (((REG(UART0 + 0x0C) >> 11) & 0x1Fu) >= 16u) {}
    REG(UART0 + 0x00) = b;
}

static void send_frame(const uint8_t *payload, uint32_t n) {
    uint32_t len = n + 4u;
    uart_put(crc8(payload, n));
    uart_put((uint8_t)len);
    uart_put((uint8_t)(len >> 8));
    uart_put(0x01u);
    for (uint32_t i = 0; i < n; i++) uart_put(payload[i]);
    ST[7]++;
}

static void send_ack(void) { /* stock 0x7b84(20): byte0 is a literal 1, not a CRC */
    uart_put(0x01u); uart_put(0x04u); uart_put(0x00u); uart_put(0x14u);
    ST[9]++;
}


static void log_frame(uint32_t cmd, const uint8_t *args, uint32_t nargs) {
    uint32_t a = 0;
    for (uint32_t i = 0; i < 4 && i < nargs; i++) a |= (uint32_t)args[i] << (8 * i);
    uint32_t n = ST[4];
    if (n && (LOGW[2 * (n - 1)] & 0xFFFFu) == cmd && LOGW[2 * (n - 1) + 1] == a
        && (LOGW[2 * (n - 1)] >> 16) < 0xFFFFu) {
        LOGW[2 * (n - 1)] += 0x10000u;
        return;
    }
    if (n >= LOG_MAX) { ST[5] = 1; return; }
    LOGW[2 * n] = cmd | 0x10000u;
    LOGW[2 * n + 1] = a;
    ST[4] = n + 1;
}

#ifdef STOCK_REPLIES
#include "../../tools/dsp_stock/stock_replies.h"
#endif
#ifdef AUDIO_PASSTHROUGH
#include "audio.h"
#endif
#ifdef RESIDENT_TICK_PATCH
#include "tickpatch.h"
#endif

/* Stock DSP-initiated status report (region c 0x7fac/0x8088): every 32 audio blocks (~43 ms) the
 * main loop sends cmd 0x4001 with 20 bytes from 0x20003bb4 (all zero when idle, per the live RAM
 * snapshot). Handler (4,1) sends the same frame on demand. */
static void send_report(void) {
    static const uint8_t r[24] = {0x01, 0x40, 24, 0}; /* + 20 zero bytes (meters idle) */
    send_frame(r, sizeof r);
    ST[12]++;
}

/* Payload = [cmd u16][len u16][arg u32 ...]; arg = id for the module queries. Replies are the
 * exact stock bytes (stock_replies.h). Unknown (cmd, arg) -> the stock reply type for cmd. */
#ifdef AUDIO_LEVEL
/* IEEE-754 single bits -> Q16 gain, 0..65536 (1.0), no FPU use. */
static uint32_t gain_q16(uint32_t f) {
    uint32_t e = (f >> 23) & 0xFFu, m = (f & 0x7FFFFFu) | 0x800000u;
    if ((f >> 31) || e < 110u) return 0;         /* negative, or below 2^-17 */
    if (e >= 127u) return 65536u;                /* 1.0 or more */
    return m >> (134u - e);                      /* m * 2^(e-150) * 2^16 */
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
#endif

#if defined(GP5_FIRMWARE) && defined(AUDIO_LEVEL)
__attribute__((unused)) static void gp5_reply(const uint8_t *payload, uint32_t n) { send_frame(payload, n); }
__attribute__((unused)) static void gp5_ack(void) { send_ack(); }
__attribute__((unused)) static uint32_t gp5_get32(const uint8_t *p) { return get32(p); }
#include "gp5_dsp.h"          /* the interface: declarations the firmware implements and may use */
#include "firmware.h"         /* firmwares/<name>/dsp/firmware.h, chosen by build.py --firmware */
#endif

static void handle(const uint8_t *p, uint32_t n) {
    uint32_t cmd = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
#ifdef AUDIO_LEVEL
    /* Output level (docs/platform/protocols.md): 0x1000 globals +0x08, or
     * 0x1001 sub 0. Our NDS32 firmware sends the volume knob this way. */
    if (cmd == 0x1000 && n >= 12) { ST[24] = gain_q16(get32(p + 8)); ST[25]++; }
    if (cmd == 0x1001 && n >= 12 && get32(p + 4) == 0) { ST[24] = gain_q16(get32(p + 8)); ST[25]++; }
#endif
    uint32_t arg = n >= 8 ? (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24) : 0;
    log_frame(cmd, p + 4, n >= 4 ? n - 4 : 0);
#ifdef RESIDENT_TICK_PATCH
    if (tp_handle(cmd, p, n)) return;
#endif
#if defined(GP5_FIRMWARE) && defined(AUDIO_LEVEL)
    if (cmd >= 0x7F10 && fw_command(cmd, p, n)) return;        /* the firmware's own commands */
#endif
#ifdef STOCK_REPLIES
    for (uint32_t i = 0; i < N_REPLIES; i++) {
        if (REPLIES[i].cmd == cmd && (REPLIES[i].arg == arg || cmd == 0x6000 || cmd == 0x6002 || cmd == 0x5002)) {
            if (REPLIES[i].len) send_frame(REPLY_BLOB + REPLIES[i].off, REPLIES[i].len);
            if (REPLIES[i].ack) send_ack();
            return;
        }
    }
#else
    (void)arg;
#endif
    switch (cmd) {
#ifdef GP5_FIRMWARE
    case 0x7F04: {                    /* DSP load: [cmd][16][cycles last block][worst since boot][late blocks] */
        uint8_t r[16] = { 0x04, 0x7F, 16, 0 };
        uint32_t v[3] = { ST[29], ST[30], ST[31] };
        for (uint32_t i = 0; i < 12; i++) r[4 + i] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
        send_frame(r, sizeof r);
        break;
    }
#endif
    case 0x6000: {                    /* version query: [cmd][len 12][1 u32][version u32], the control side waits for it */
        static const uint8_t r[12] = { 0x00, 0x60, 12, 0, 1, 0, 0, 0,
                                       (uint8_t)GP5_PLATFORM_VERSION, (uint8_t)(GP5_PLATFORM_VERSION >> 8),
                                       (uint8_t)(GP5_PLATFORM_VERSION >> 16), (uint8_t)(GP5_PLATFORM_VERSION >> 24) };
        send_frame(r, sizeof r);
        ST[7]++;
        break;
    }
    case 0x4001:
        send_report();
        break;
    case 0x0002: case 0x5000: case 0x7004:
        ST[10]++;                     /* stock never replies to these */
        break;
    default:
        ST[11]++;                     /* not in the table (incl. unseen ids): stock "set" ACK */
        send_ack();
        break;
    }
}

void resp_main(void) {
    REG(0xE000ED08u) = 0x4400u;
    uint32_t boots;
    if (ST[0] == MAGIC) {
        boots = ST[2] + 1u;
    } else {
        boots = 1u;
        for (int i = 4; i < 32; i++) ST[i] = 0;
    }
    ST[1] = 0;
    ST[2] = boots;
    ST[3] = 0;
    ST[0] = MAGIC;
    ST[24] = 0;                       /* output muted until the NDS32 sends a level */
    ST[25] = 0;
    ST[26] = 0;
    ST[29] = 0; ST[30] = 0; ST[31] = 0;

    REG(UART0 + 0x10) = (UART_CLK + BAUD * 8u) / (BAUD * 16u) - 1u;
    REG(UART0 + 0x08) = 0x202Eu;
    REG(UART0 + 0x04) = 0x703u;
    REG(UART0 + 0x14) = 0x5000u;

    /* SysTick, polled (no TICKINT): COUNTFLAG every ~2.75M core cycles = 43 ms at 64 MHz,
     * 14 ms at 192 MHz (core clock under the resident is not pinned yet). */
    REG(0xE000E010u) = 0;
    REG(0xE000E014u) = 2750000u - 1u;
    REG(0xE000E018u) = 0;
    REG(0xE000E010u) = 5u;

#ifdef AUDIO_PASSTHROUGH
#ifdef AUDIO_SPI1
    spi1_init();
#endif
    audio_init();
#endif
    uint8_t *const buf = (uint8_t *)0x20006C00u;
    const uint32_t BUF_MAX = 512u;
    uint32_t have = 0;
    for (;;) {
        ST[1]++;
#ifdef AUDIO_PASSTHROUGH
        audio_poll();
#endif
        if (REG(0xE000E010u) & 0x10000u) send_report();
        uint32_t n = (REG(UART0 + 0x0C) >> 4) & 0x1Fu;
        while (n--) {
            uint8_t b = (uint8_t)REG(UART0 + 0x00);
            if (have < BUF_MAX) buf[have++] = b;
        }
        while (have >= 4u) {
            uint32_t len = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8);
            if (len < 6u || len > BUF_MAX || buf[3] != 0x01u) {
                for (uint32_t k = 1; k < have; k++) buf[k - 1] = buf[k];
                have--;
                continue;
            }
            if (have < len) break;
            if (crc8(buf + 4, len - 4u) == buf[0]) { ST[6]++; handle(buf + 4, len - 4u); }
            else ST[8]++;
            for (uint32_t k = len; k < have; k++) buf[k - len] = buf[k];
            have -= len;
        }
    }
}

void default_handler(void) {
    ST[3] = 0xDEAD0000u | (REG(0xE000ED04u) & 0x1FFu);
    for (;;) {}
}

__attribute__((section(".vectors"), used))
void (*const vectors[16 + 48])(void) = {
    (void (*)(void))0x20006C00u,
    reset_handler,
    [2 ... 16 + 47] = default_handler,
};
