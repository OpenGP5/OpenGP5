/* GP-5 open platform, control processor (BP1048 / NDS32): the main program.
 *
 * Start-up (start/) hands over to main(), which brings the chip up (hal/clock.c), sets the board's pins as
 * Valeton's app leaves them, starts the screen (ST7735S-type, 160 x 80, SPI master port 0), the backlight,
 * the LEDs, USB-MIDI (usb_midi.c) and the link to the DSP (dsp_link.c), then loops:
 *   - answers USB requests (proto.c: restart into update mode, DSP link bridge);
 *   - waits for the DSP's version answer, sends it the output level (dsp_ctl.c) and calls fw_dsp_ready();
 *   - reads the foot switch, the PARA knob (turn + push) and the volume knob, and calls the firmware's
 *     fw_foot / fw_para_turn / fw_para_push / fw_knob (gp5_firmware.h);
 *   - sends the settings the firmware hands over (fw_take) to the DSP as link command 0x7F10;
 *   - redraws the screen through fw_draw.
 * Pins and protocols: docs/platform/hardware.md, docs/platform/protocols.md.
 *
 * Recovery: hold Foot+PARA at power-on (handled by the factory loader, before this code runs). */
#include <nds32_intrinsic.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal/gpio.h"
#include "hal/wdg.h"
#include "hal/spim.h"
#include "hal/adc.h"
#include "hal/timer.h"
#include "hal/tick.h"
#include "hal/backlight.h"
#include "hal/clock.h"
#include "hal/flash.h"
#include "start/irq.h"
#include "usb_midi.h"
#include "proto.h"
void gp5_link_stop(void);                    /* audio_link.c */
int gp5_link_running(void);
/* The hand-over mark: the top 32 bytes of RAM, outside the app's RAM (start/app.ld). Survives a system reset. */
#define HANDOVER_MARK  ((volatile uint32_t *)0x20047FE0u)
#define HANDOVER_MAGIC 0x4C445231u                   /* "LDR1" */
#include "dsp_link.h"
#include "dsp_ctl.h"
#include "usb_audio.h"
#include "gp5_firmware.h"
#include "gp5_params.h"
#ifdef GP5_HAS_PARAMS                                         /* gp5_params.c: the firmware's parameters (1.0) */
void gp5_params_init(void); void gp5_params_poll(void); int gp5_params_take(uint32_t *id, uint32_t *value);
void gp5_params_dsp_restart(void); uint32_t gp5_params_dsp_mask(void); int gp5_params_set_outside(unsigned id, int32_t value);
int gp5_params_store_changed(uint32_t id);
#endif
extern const uint8_t gp5_meta[] __attribute__((weak));
extern volatile uint32_t gp5_link_cycles, gp5_link_worst;   /* audio_link.c */
extern volatile uint32_t gp5_link_blocks, gp5_link_lost;
void gp5_link_check(void);                                   /* audio_link.c */
extern char _end[];                                          /* app.ld: the end of this firmware's RAM */
static uint32_t loop_worst, loop_skip;
unsigned gp5_log_read(uint32_t from, uint8_t *out, unsigned max, uint32_t *at, uint32_t *end);   /* gp5_log.c */
static int api_store_changed(uint32_t id);              /* the firmware's callbacks: below */   /* the manifest's metadata block (out/gen/gp5_meta.c), if any */
#include "led.h"


#define DC_PIN   GP5_PIN(7)    /* A7  low = command (the SPIM port-0 MISO pin, taken back as GPIO) */
#define CS_PIN   GP5_PIN(9)    /* A9  low = selected */
#define RST_PIN  GP5_PIN(16)   /* A16 low = reset */
#define LCD_PINS (DC_PIN | CS_PIN | RST_PIN)

#define W 160
#define H 80

/* Busy-loop delay. Sized for the PLL clock (100,000 a millisecond at 288 MHz); if the PLL did not lock (12 MHz RC)
 * it only runs long, never short. */
#include "hal/clock.h"
#define ITERS_PER_MS (100000u * GP5_CTL_MHZ / 288u)
static void delay_ms(unsigned ms)
{
    volatile unsigned n = ms * ITERS_PER_MS;
    while (n--) { __asm__ volatile(""); }
}

/* one port-A pin (given as its bit) as a plain digital pad, pad settings as stock: hal/gpio.c */
static void out_pin(uint32_t mask)
{
    unsigned n = 0;
    while (!(mask & 1u)) { mask >>= 1; n++; }
    gp5_gpio_a_digital(n);
}

static void board_init(void)
{
    /* 0x47d90: A24 then A22 as outputs; A22 high, A24 low */
    out_pin(GP5_PIN(24));
    out_pin(GP5_PIN(22));
    gp5_reg_bits_set(GP5_GPIO_A_PU, GP5_PIN(22) | GP5_PIN(24));
    gp5_reg_bits_set(GP5_GPIO_A_PD, GP5_PIN(22) | GP5_PIN(24));
    gp5_reg_bits_clear(GP5_GPIO_A_IE, GP5_PIN(22) | GP5_PIN(24));
    gp5_reg_bits_set(GP5_GPIO_A_OE, GP5_PIN(22) | GP5_PIN(24));
    gp5_reg_bits_set(GP5_GPIO_A_OUT, GP5_PIN(22));
    gp5_reg_bits_clear(GP5_GPIO_A_OUT, GP5_PIN(24));
    delay_ms(50);

    /* 0x47f44: A15 output, low (stock raises it after the first frame, see main) */
    out_pin(GP5_PIN(15));
    gp5_reg_bits_set(GP5_GPIO_A_PU, GP5_PIN(15));
    gp5_reg_bits_set(GP5_GPIO_A_PD, GP5_PIN(15));
    gp5_reg_bits_clear(GP5_GPIO_A_IE, GP5_PIN(15));
    gp5_reg_bits_set(GP5_GPIO_A_OE, GP5_PIN(15));
    gp5_reg_bits_clear(GP5_GPIO_A_OUT, GP5_PIN(15));

    /* 0x47fbc: B5 output, low */
    gp5_gpio_b_digital(5);
    gp5_reg_bits_set(GP5_GPIO_B_PU, GP5_PIN(5));
    gp5_reg_bits_set(GP5_GPIO_B_PD, GP5_PIN(5));
    gp5_reg_bits_clear(GP5_GPIO_B_IE, GP5_PIN(5));
    gp5_reg_bits_set(GP5_GPIO_B_OE, GP5_PIN(5));
    gp5_reg_bits_clear(GP5_GPIO_B_OUT, GP5_PIN(5));

    /* 0x47e20: SPIM on port 0 (A5/A6/A7), THEN A7 (the port's MISO) back to GPIO for DC */
    gp5_spim_pins_port0();
    out_pin(DC_PIN);
    out_pin(CS_PIN);
    out_pin(RST_PIN);
    gp5_reg_bits_set(GP5_GPIO_A_PU, LCD_PINS);
    gp5_reg_bits_clear(GP5_GPIO_A_PD, LCD_PINS);
    gp5_reg_bits_clear(GP5_GPIO_A_IE, LCD_PINS);
    gp5_reg_bits_set(GP5_GPIO_A_OE, LCD_PINS);
    gp5_reg_bits_set(GP5_GPIO_A_OUT, DC_PIN);
    gp5_reg_bits_set(GP5_GPIO_A_OUT, CS_PIN);
    gp5_reg_bits_set(GP5_GPIO_A_OUT, RST_PIN);
}

/* 0x48590 + 0x485ec: backlight = TIMER5 PWM on A10, duty in percent */
static void backlight(unsigned percent)
{
    gp5_backlight_on(percent);                   /* timer 5 pulse-width output on A10 (hal/backlight.c) */
}

/* Keep FW12's proven display writes while LEDs are stopped. Once the LED IRQ is
 * active, hardware SET/CLR aliases prevent either writer restoring stale bits.
 * Only the main loop starts the IRQ; a faulted LED driver never re-enables it. */
static void lcd_pin(uint32_t pins, int high)
{
    if (gp5_led_status == 1) gp5_reg_set(high ? GP5_GPIO_A_SET : GP5_GPIO_A_CLR, pins);
    else if (high) gp5_reg_bits_set(GP5_GPIO_A_OUT, pins);
    else gp5_reg_bits_clear(GP5_GPIO_A_OUT, pins);
}

static void lcd_cmd(uint8_t b)
{
    lcd_pin(DC_PIN, 0);
    lcd_pin(CS_PIN, 0);
    gp5_spim_send(&b, 1);
    lcd_pin(CS_PIN, 1);
    lcd_pin(DC_PIN, 1);
}

static void lcd_dat(uint8_t *b, unsigned n)
{
    lcd_pin(CS_PIN, 0);
    gp5_spim_send(b, n);
    lcd_pin(CS_PIN, 1);
}

/* stock init sequence (@0x48330) as {cmd, n_args, args...}; n_args 0xFF ends */
static const uint8_t initseq[] = {
    0x11, 0,  0x21, 0,
    0xB1, 3, 0x05,0x3A,0x3A,  0xB2, 3, 0x05,0x3A,0x3A,
    0xB3, 6, 0x05,0x3A,0x3A,0x05,0x3A,0x3A,  0xB4, 1, 0x03,
    0xC0, 3, 0x62,0x02,0x04,  0xC1, 1, 0xC0,  0xC2, 2, 0x0D,0x00,
    0xC3, 2, 0x8D,0x6A,  0xC4, 2, 0x8D,0xEE,  0xC5, 1, 0x0E,
    0xE0, 16, 0x10,0x0E,0x02,0x03,0x0E,0x07,0x02,0x07,0x0A,0x12,0x27,0x37,0x00,0x0D,0x0E,0x10,
    0xE1, 16, 0x10,0x0E,0x03,0x03,0x0F,0x06,0x02,0x08,0x0A,0x13,0x26,0x36,0x00,0x0D,0x0E,0x10,
    0x3A, 1, 0x05,  0x36, 1, 0xA0,  0x28, 0,  0x29, 0,     /* 0x36 = 0xA0: rotation; colour order bit clear (0xA8 swapped red and blue) */
    0x00, 0xFF
};

static void panel_init(void)
{
    uint8_t args[16];
    const uint8_t *p = initseq;
    gp5_reg_bits_set(GP5_GPIO_A_OUT, RST_PIN);   delay_ms(1);
    gp5_reg_bits_clear(GP5_GPIO_A_OUT, RST_PIN); delay_ms(1);
    gp5_reg_bits_set(GP5_GPIO_A_OUT, RST_PIN);   delay_ms(120);
    for (;;) {
        uint8_t cmd = p[0], n = p[1], i;
        if (n == 0xFF) break;
        lcd_cmd(cmd);
        for (i = 0; i < n; i++) args[i] = p[2 + i];   /* SPIM_Send takes a non-const buffer */
        if (n) lcd_dat(args, n);
        p += 2 + n;
    }
}

static void set_window(unsigned x0, unsigned x1, unsigned y0, unsigned y1)
{
    uint8_t a[4];
    x0 += 1; x1 += 1; y0 += 26; y1 += 26;             /* panel offsets (stock fill_rect @0x48268) */
    lcd_cmd(0x2A); a[0] = x0 >> 8; a[1] = x0; a[2] = x1 >> 8; a[3] = x1; lcd_dat(a, 4);
    lcd_cmd(0x2B); a[0] = y0 >> 8; a[1] = y0; a[2] = y1 >> 8; a[3] = y1; lcd_dat(a, 4);
    lcd_cmd(0x2C);
}

static uint8_t row[W * 2];

/* colours are given as R,G,B; the panel takes blue in the high bits (BGR 5-6-5), red in the low bits */
static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
}

static void fill_rect(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c)
{
    unsigned i, j;
    for (i = 0; i < w; i++) { row[2 * i] = (uint8_t)(c >> 8); row[2 * i + 1] = (uint8_t)c; }
    set_window(x, x + w - 1, y, y + h - 1);
    for (j = 0; j < h; j++) lcd_dat(row, w * 2);
}

/* The start-up picture: OpenGP5 logo, 160 x 80, run-length encoded in the panel's colour format
 * (boot_logo.h, made from boot_logo.png by tools/png_to_screen.py). */
#include "boot_logo.h"

static void draw_logo(void)
{
    unsigned r = 0, left = boot_logo[0].count, x, y;
    set_window(0, W - 1, 0, H - 1);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            uint16_t c = boot_logo[r].colour;
            row[2 * x] = (uint8_t)(c >> 8); row[2 * x + 1] = (uint8_t)c;
            if (--left == 0 && ++r < BOOT_LOGO_RUNS) left = boot_logo[r].count;
        }
        lcd_dat(row, W * 2);
    }
}

#include "font5x7.h"

/* text cell: 6x8 font cell at 2x = 12x16 px -> 13 columns x 5 rows on the 160x80 panel */
#define CW 12
#define CH 16
#define COLS 13
#define ROWS 5
static uint8_t cell[CW * CH * 2];

static void draw_char(unsigned col, unsigned line, char ch, uint16_t fg, uint16_t bg)
{
    const uint8_t *g;
    unsigned x, y;
    if (ch < 0x20 || ch > GP5_FONT5X7_LAST) ch = '?';
    g = font[ch - 0x20];
    for (y = 0; y < CH; y++) {
        for (x = 0; x < CW; x++) {
            unsigned fx = x / 2, fy = y / 2;
            uint16_t c = (fx < 5 && fy < 8 && ((g[fx] >> fy) & 1)) ? fg : bg;
            cell[2 * (y * CW + x)] = (uint8_t)(c >> 8); cell[2 * (y * CW + x) + 1] = (uint8_t)c;
        }
    }
    set_window(2 + col * CW, 2 + col * CW + CW - 1, line * CH, line * CH + CH - 1);
    lcd_dat(cell, sizeof cell);
}

/* what is on screen (character and colours), so only changed cells are redrawn */
static char shown[ROWS][COLS];
static uint32_t shown_col[ROWS][COLS];

static void put_line(unsigned line, const char *s, uint16_t fg, uint16_t bg)
{
    unsigned i, end = 0;
    uint32_t col = ((uint32_t)fg << 16) | bg;
    for (i = 0; i < COLS; i++) {
        char ch = ' ';
        if (!end) { if (s[i]) ch = s[i]; else end = 1; }
        if (shown[line][i] != ch || shown_col[line][i] != col) {
            draw_char(i, line, ch, fg, bg); shown[line][i] = ch; shown_col[line][i] = col;
        }
    }
}

static char *hex(char *p, uint32_t v, unsigned digits)
{
    while (digits--) *p++ = "0123456789ABCDEF"[(v >> (4 * digits)) & 0xF];
    return p;
}

/* Control map, confirmed on hardware with an input monitor (2026-11-09) */
#define DEBOUNCE_MS 30u          /* foot switch and PARA push: ignore contact bounce this long after a change */
#define AUDIO_WAIT_MS 3000u                   /* the most the sound waits for the start-up settings */
#define FOOT_PIN  GP5_PIN(17)    /* port A: foot switch, active low */
#define PARA_PIN  GP5_PIN(6)     /* port B: PARA knob push button, active low */
#define ENC_PINS  (GP5_PIN(2) | GP5_PIN(3))   /* port B: PARA knob quadrature (both high at a detent) */

static void io_init(void)
{
    /* inputs with pull-up: IE set, OE clear, PU set, PD clear (stock @0x47ee4 for B6 and A17) */
    gp5_reg_bits_set(GP5_GPIO_B_IE, PARA_PIN | ENC_PINS);
    gp5_reg_bits_clear(GP5_GPIO_B_OE, PARA_PIN | ENC_PINS);
    gp5_reg_bits_set(GP5_GPIO_B_PU, PARA_PIN | ENC_PINS);
    gp5_reg_bits_clear(GP5_GPIO_B_PD, PARA_PIN | ENC_PINS);
    gp5_reg_bits_set(GP5_GPIO_A_IE, FOOT_PIN);
    gp5_reg_bits_clear(GP5_GPIO_A_OE, FOOT_PIN);
    gp5_reg_bits_set(GP5_GPIO_A_PU, FOOT_PIN);
    gp5_reg_bits_clear(GP5_GPIO_A_PD, FOOT_PIN);

    /* LEDs: outputs, low (stock @0x4800c) */
    out_pin(GP5_PIN(31));
    out_pin(GP5_PIN(30));
    out_pin(GP5_PIN(29));
    gp5_reg_bits_set(GP5_GPIO_A_PU, LED_PINS);
    gp5_reg_bits_set(GP5_GPIO_A_PD, LED_PINS);
    gp5_reg_bits_clear(GP5_GPIO_A_IE, LED_PINS);
    gp5_reg_bits_set(GP5_GPIO_A_OE, LED_PINS);
    gp5_reg_bits_clear(GP5_GPIO_A_OUT, LED_PINS);
}

/* Progress marker for the debugger: read `gp5_stage` (address in out/app.sym) after a halt. */
volatile uint32_t gp5_stage;

static void midi_send(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t ev[4];
    ev[0] = status >> 4;                  /* USB-MIDI event: code index = status high nibble, cable 0 */
    ev[1] = status; ev[2] = d1; ev[3] = d2;
    usb_midi_write(ev, 4);
}


static uint8_t sat8(uint32_t v) { return v > 255 ? 255 : (uint8_t)v; }

/* Volume knob: a pot on A28 = SAR ADC channel 9, set up and filtered as stock does it
 * (0x5a450: clock divider 7, Vref 1, mode 0, calibration; 0x47ce8: analog enable for A28;
 * 0x47d10 every 10 ms: filt = (sample + 3 * filt) / 4, a new value is taken when it differs from the
 * last one by 64 or more at once, or by 16 or more for over 20 readings). Value = 12 bits >> 4. */
static uint32_t knob_filt, knob_last;
static uint8_t knob_cnt;

static void knob_init(void)
{
    gp5_adc_init();                              /* clock divider setting 7, reference = supply, calibrated */
    gp5_reg_bits_set(GP5_GPIO_A_ANA_EN, GP5_PIN(28));
}

static void knob_poll(void)
{
    uint32_t v = (uint16_t)gp5_adc_read(9), d;

    knob_filt = (v + 3 * knob_filt) >> 2;
    d = knob_filt > knob_last ? knob_filt - knob_last : knob_last - knob_filt;
    if (!(d >> 4)) { knob_cnt = 0; return; }
    if (knob_cnt++ < 21 && !(d >> 6)) return;
    knob_last = knob_filt;
    knob_cnt = 0;
}

static unsigned knob_value(void) { return (knob_last >> 4) & 0xFF; }

/* Host commands 7E 01 .. 7E 05 (proto.c); the request is in proto_req. */
static uint8_t dsp_raw[DSP_RAW_MAX];     /* payload staged by 7E 03, sent by 7E 04 */
static void host_dsp_cmd(void)
{
    static uint8_t pl[PROTO_REPLY_MAX];
    unsigned n = 0, i;

    pl[n++] = 0x7E; pl[n++] = proto_req[1];
    if (proto_req[1] == 0x01 && proto_req_len == 2) {
        /* frames u16, unsolicited frames u16, last unsolicited command u16, then one byte each (255 = or
         * more): UART errors, bad frames, ring overflows, ACKs, NAKs, interrupt-off flag, bytes u16;
         * then the last unsolicited frame's payload. */
        pl[n++] = (uint8_t)dsp_rx_frames; pl[n++] = (uint8_t)(dsp_rx_frames >> 8);
        pl[n++] = (uint8_t)dsp_unsol_frames; pl[n++] = (uint8_t)(dsp_unsol_frames >> 8);
        pl[n++] = (uint8_t)dsp_unsol_cmd; pl[n++] = (uint8_t)(dsp_unsol_cmd >> 8);
        pl[n++] = sat8(dsp_rx_errs); pl[n++] = sat8(dsp_rx_bad); pl[n++] = sat8(dsp_rx_lost);
        pl[n++] = sat8(dsp_acks); pl[n++] = sat8(dsp_naks); pl[n++] = (uint8_t)dsp_irq_off;
        pl[n++] = (uint8_t)dsp_rx_bytes; pl[n++] = (uint8_t)(dsp_rx_bytes >> 8);
        for (i = 0; i < dsp_unsol_len; i++) pl[n++] = dsp_unsol[i];
    } else if (proto_req[1] == 0x02 && proto_req_len == 5) {
        uint8_t args[6] = { 8, 0, 0, 0, 0, 0 };
        args[2] = proto_req[4];
        pl[2] = (uint8_t)dsp_link_query((uint16_t)(proto_req[2] | (proto_req[3] << 8)), args, sizeof args,
                                        pl + 3, sizeof pl - 3, 100);
        n = 3 + pl[2];
    } else if (proto_req[1] == 0x03 && proto_req_len >= 5) {
        unsigned off = proto_req[2] | (proto_req[3] << 8), cnt = proto_req_len - 4;
        pl[2] = 0;
        if (off + cnt <= sizeof dsp_raw) {
            for (i = 0; i < cnt; i++) dsp_raw[off + i] = proto_req[4 + i];
            pl[2] = 1;
        }
        n = 3;
    } else if (proto_req[1] == 0x04 && proto_req_len == 5) {
        pl[3] = (uint8_t)dsp_link_raw(dsp_raw, proto_req[2] | (proto_req[3] << 8), pl + 4, sizeof pl - 4,
                                      proto_req[4] * 10u, &pl[2]);
        n = 4 + pl[3];
    } else if (proto_req[1] == 0x05 && proto_req_len == 7) {
        /* read flash (memory-mapped, read-only): 7E 05 <addr u32> <count <= 64> -> 7E 05 <ok> <bytes> */
        uint32_t a = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24);
        unsigned cnt = proto_req[6];
        pl[2] = 0; n = 3;
        if (cnt <= 64 && a < 0x200000u && a + cnt <= 0x200000u) {
            for (i = 0; i < cnt; i++) pl[n++] = *(volatile const uint8_t *)(a + i);
            pl[2] = 1;
        }
#ifdef GP5_DEBUG_PEEK
    } else if (proto_req[1] == 0x0A && proto_req_len == 7) {
        /* debug builds only (GP5_DEBUG_PEEK=1): read words anywhere, registers included:
         * 7E 0A <addr u32, 4-aligned> <count <= 16> -> 7E 0A <ok> <words LE> */
        uint32_t a = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24);
        unsigned cnt = proto_req[6];
        pl[2] = 0; n = 3;
        if (cnt <= 16 && !(a & 3u)) {
            for (i = 0; i < cnt; i++) {
                uint32_t w = *(volatile const uint32_t *)(a + 4u * i);
                pl[n++] = (uint8_t)w; pl[n++] = (uint8_t)(w >> 8); pl[n++] = (uint8_t)(w >> 16); pl[n++] = (uint8_t)(w >> 24);
            }
            pl[2] = 1;
        }
    } else if (proto_req[1] == 0x0C && proto_req_len == 10) {
        /* debug builds only: write one word: 7E 0C <addr u32, 4-aligned> <value u32> -> 7E 0C <ok> */
        uint32_t a = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24);
        uint32_t v = proto_req[6] | (proto_req[7] << 8) | ((uint32_t)proto_req[8] << 16) | ((uint32_t)proto_req[9] << 24);
        pl[2] = !(a & 3u); n = 3;
        if (pl[2]) *(volatile uint32_t *)a = v;
    } else if (proto_req[1] == 0x0B && proto_req_len == 2) {
        /* 7E 0B -> 7E 0B 1 <INT_MASK2> <INT_PEND2> <INT_PRI> <INT_PRI2> <PSW> */
        uint32_t v[5] = { __nds32__mfsr(NDS32_SR_INT_MASK2), __nds32__mfsr(NDS32_SR_INT_PEND2),
                          __nds32__mfsr(NDS32_SR_INT_PRI), __nds32__mfsr(NDS32_SR_INT_PRI2), __nds32__mfsr(NDS32_SR_PSW) };
        pl[2] = 1; n = 3;
        for (i = 0; i < 20; i++) pl[n++] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
#endif
    } else if (proto_req[1] == 0x06 && proto_req_len == 2) {
        /* flash id and status: 7E 06 -> 7E 06 1 <JEDEC id u32> <status u32> */
        uint32_t id = gp5_flash_id(), st = gp5_flash_status();
        pl[2] = 1;
        for (i = 0; i < 4; i++) { pl[3 + i] = (uint8_t)(id >> (8 * i)); pl[7 + i] = (uint8_t)(st >> (8 * i)); }
        n = 11;
    } else if (proto_req[1] == 0x09 && proto_req_len == 7) {
        /* flash read through the controller's read command: 7E 09 <addr u32> <count> -> 7E 09 <ok> <bytes> */
        static uint32_t r[16];
        uint32_t a = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24);
        pl[2] = gp5_flash_read_cmd(a, r, proto_req[6]) == 0;
        n = 3;
        if (pl[2]) for (i = 0; i < proto_req[6]; i++) pl[n++] = ((uint8_t *)r)[i];
    } else if ((proto_req[1] == 0x07 && proto_req_len == 6) || (proto_req[1] == 0x08 && proto_req_len >= 10)) {
        /* store and data areas only (hal/flash.h): 7E 07 <addr u32> erase a 4 KB sector; 7E 08 <addr u32> <bytes, a
         * multiple of 4> program -> 7E 07|08 <ok> */
        static uint32_t w[16];
        uint32_t a = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24);
        unsigned cnt = proto_req_len - 6;
        if (proto_req[1] == 0x07) pl[2] = gp5_flash_erase(a) == 0;
        else if (cnt > sizeof w || (cnt & 3u)) pl[2] = 0;
        else {
            for (i = 0; i < cnt; i++) ((uint8_t *)w)[i] = proto_req[6 + i];
            pl[2] = gp5_flash_program(a, w, cnt) == 0;
        }
        dsp_link_rx_reset();                         /* interrupts were off: the DSP link's receiver may be stuck */
        n = 3;
    } else if (proto_req[1] == 0x0D && proto_req_len == 2) {
        /* who is this: 7E 0D -> 7E 0D 1 <platform major minor patch> <firmware name, ASCII, zero-terminated> */
        const char *nm = fw_name();
        pl[2] = 1; pl[3] = GP5_VERSION_MAJOR; pl[4] = GP5_VERSION_MINOR; pl[5] = GP5_VERSION_PATCH; n = 6;
        for (i = 0; nm[i] && i < 32; i++) pl[n++] = (uint8_t)nm[i];
        pl[n++] = 0;
    } else if (proto_req[1] == 0x16 && proto_req_len == 4) {
        /* the firmware's metadata block (1.0, platform/manifest.py): 7E 16 <offset u16> -> 7E 16 <1> <total u16>
         * <up to 48 bytes from offset>; 7E 16 <0> for a firmware built without a manifest */
        uint32_t off = proto_req[2] | (proto_req[3] << 8), total;
        if (!gp5_meta) { pl[2] = 0; n = 3; }
        else {
            total = 12u + (gp5_meta[8] | (gp5_meta[9] << 8)) + 4u;
            pl[2] = 1; pl[3] = (uint8_t)total; pl[4] = (uint8_t)(total >> 8); n = 5;
            for (i = 0; i < 48u && off + i < total; i++) pl[n++] = gp5_meta[off + i];
        }
#ifdef GP5_HAS_PARAMS
    } else if (proto_req[1] == 0x12 && proto_req_len == 4) {
        /* describe a parameter (1.0): 7E 12 <id u16> -> 7E 12 1 <count u16> <id u16> <flags> <unit> <min i16> <max i16>
         * <default i16> <name\0> <group\0> <choices\0>  (0 past the last one) */
        uint32_t id = proto_req[2] | (proto_req[3] << 8);
        if (id >= GP5_NPARAMS) { pl[2] = 0; n = 3; }
        else {
            const gp5_param_desc_t *d = &gp5_param_table[id];
            const char *t[3] = { d->name, d->group ? d->group : "", d->choices ? d->choices : "" };
            static const uint8_t lim[3] = { 15, 15, 47 };
            pl[2] = 1; pl[3] = GP5_NPARAMS & 0xFF; pl[4] = GP5_NPARAMS >> 8; pl[5] = (uint8_t)id; pl[6] = (uint8_t)(id >> 8);
            pl[7] = d->flags; pl[8] = d->unit;
            pl[9] = (uint8_t)d->min; pl[10] = (uint8_t)((uint16_t)d->min >> 8); pl[11] = (uint8_t)d->max; pl[12] = (uint8_t)((uint16_t)d->max >> 8);
            pl[13] = (uint8_t)d->def; pl[14] = (uint8_t)((uint16_t)d->def >> 8); n = 15;
            for (unsigned k = 0; k < 3; k++) { for (i = 0; t[k][i] && i < lim[k]; i++) pl[n++] = (uint8_t)t[k][i]; pl[n++] = 0; }
        }
    } else if (proto_req[1] == 0x15 && proto_req_len == 6) {
        /* a value's text (GP5_U_CUSTOM): 7E 15 <id u16> <value i16> -> 7E 15 <1> <text\0>, 7E 15 0 if the firmware has none */
        uint32_t id = proto_req[2] | (proto_req[3] << 8);
        char txt[16] = { 0 };
        int32_t v = (int16_t)(proto_req[4] | (proto_req[5] << 8));
        if (id < GP5_NPARAMS && fw_param_text(id, v, txt)) { pl[2] = 1; n = 3; for (i = 0; txt[i] && i < 15; i++) pl[n++] = (uint8_t)txt[i]; pl[n++] = 0; }
        else { pl[2] = 0; n = 3; }
    } else if (proto_req[1] == 0x17 && proto_req_len >= 3) {
        /* presets (1.0): 7E 17 0 -> 7E 17 1 <count> <current, 0xFF none> <dirty> <used u64>;  7E 17 1 <n> load;
         * 7E 17 2 <n> save;  7E 17 3 <n> -> <name\0>;  7E 17 4 <n> <name...> rename  (-> 7E 17 <1 done / 0>) */
        unsigned op = proto_req[2], pn = proto_req_len > 3 ? proto_req[3] : 0u;
        pl[2] = 0; n = 3;
        if (op == 0) {
            uint64_t u = 0; int c = gp5_preset_current();
            for (i = 0; i < gp5_preset_count(); i++) if (gp5_preset_used(i)) u |= 1ull << i;
            pl[2] = 1; pl[3] = (uint8_t)gp5_preset_count(); pl[4] = c < 0 ? 0xFF : (uint8_t)c; pl[5] = (uint8_t)gp5_preset_dirty(); n = 6;
            for (i = 0; i < 8; i++) pl[n++] = (uint8_t)(u >> (8 * i));
        } else if (op == 1 && proto_req_len == 4) { pl[2] = (uint8_t)gp5_preset_load(pn); }
        else if (op == 2 && proto_req_len == 4) pl[2] = (uint8_t)gp5_preset_save(pn);
        else if (op == 3 && proto_req_len == 4) { const char *nm = gp5_preset_name(pn); pl[2] = 1; for (i = 0; nm[i]; i++) pl[n++] = (uint8_t)nm[i]; pl[n++] = 0; }
        else if (op == 4 && proto_req_len >= 4) {
            char nm[13] = { 0 };
            for (i = 0; i < 12u && 4u + i < proto_req_len; i++) nm[i] = (char)proto_req[4 + i];
            pl[2] = (uint8_t)gp5_preset_rename(pn, nm);
        }
#endif
    } else if ((proto_req[1] == 0x14 && (proto_req_len == 2 || proto_req_len == 3))) {
        /* load and memory of both processors (1.0): 7E 14 [1 = then start the worst values again] -> 7E 14 1
         * <15 u32: control: cycles per block, audio link handler last, worst, main loop longest pass, RAM used, RAM total;
         *  DSP answered (1/0), cycles per block, last, worst, late blocks, variables used, variables total,
         *  GP5_LARGE used, GP5_LARGE total> */
        uint8_t rep[28];
        uint32_t v[15], k;
        static const uint8_t reset_dsp[6] = { 8, 0, 1, 0, 0, 0 };   /* [len u16][1]: the payload's own length first */
        int reset = proto_req_len == 3 && proto_req[2] == 1;
        unsigned got = dsp_link_query(0x7F04, reset ? reset_dsp : 0, reset ? 6u : 0u, rep, sizeof rep, 30);
        loop_skip = 1;
        v[0] = GP5_CTL_MHZ * 1000000u / GP5_SAMPLE_RATE * 64u; v[1] = gp5_link_cycles; v[2] = gp5_link_worst; v[3] = loop_worst;
        v[4] = (uint32_t)((uintptr_t)_end - 0x20004000u); v[5] = 0x44000u - 0x20u;
        v[6] = got >= 16u; v[7] = 191900000u / GP5_SAMPLE_RATE * 64u;
        for (k = 0; k < 3; k++) v[8 + k] = got >= 16u ? (uint32_t)(rep[4 + 4 * k] | rep[5 + 4 * k] << 8 | rep[6 + 4 * k] << 16 | (uint32_t)rep[7 + 4 * k] << 24) : 0u;
        for (k = 0; k < 2; k++) v[11 + 2 * k] = got >= 24u ? (uint32_t)(rep[16 + 4 * k] | rep[17 + 4 * k] << 8 | rep[18 + 4 * k] << 16 | (uint32_t)rep[19 + 4 * k] << 24) : 0u;
        v[12] = 19968u; v[14] = 124928u;
        pl[2] = 1; n = 3;
        for (k = 0; k < 15; k++) for (i = 0; i < 4; i++) pl[n++] = (uint8_t)(v[k] >> (8 * i));
        if (reset) { gp5_link_worst = 0; loop_worst = 0; }
    } else if (proto_req[1] == 0x19 && (proto_req_len == 2 || proto_req_len == 3)) {
        /* USB audio (1.0): 7E 19 [0 = the pedal's own sound off while the computer plays, 1 = on] -> 7E 19 1 <direct
         * 0/1> <computer playing 0/1> <playback: ran dry u32> <overflowed u32> <clock correction, ppm i32>
         * <frames waiting u16> <recording: overflowed u32> <recording on 0/1>; 7E 19 0 without USB audio */
#ifdef GP5_USB_AUDIO
        if (proto_req_len == 3) gp5_usb_audio_set_direct(proto_req[2] != 0);
        pl[2] = 1; pl[3] = (uint8_t)gp5_usb_audio_direct(); pl[4] = (uint8_t)usb_audio_get_interface(USB_AUDIO_IF_PLAY); n = 5;
        {   uint32_t w[3] = { usb_audio_p_under, usb_audio_p_over, (uint32_t)usb_audio_ppm }, k;
            for (k = 0; k < 3; k++) for (i = 0; i < 4; i++) pl[n++] = (uint8_t)(w[k] >> (8 * i));
            pl[n++] = (uint8_t)usb_audio_p_fill; pl[n++] = (uint8_t)(usb_audio_p_fill >> 8);
            for (i = 0; i < 4; i++) pl[n++] = (uint8_t)(usb_audio_r_over >> (8 * i));
            pl[n++] = (uint8_t)usb_audio_get_interface(USB_AUDIO_IF_REC);
        }
#else
        pl[2] = 0; n = 3;
#endif
    } else if (proto_req[1] == 0x18 && proto_req_len == 6) {
        /* the log (1.0, gp5_log.c): 7E 18 <from u32> -> 7E 18 1 <at u32> <end u32> <n> <n bytes from `at`>; `at` is
         * `from`, or the oldest byte still kept if `from` was overwritten; `end` = bytes written since start */
        uint32_t from = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24), at, end;
        unsigned got = gp5_log_read(from, pl + 12, 48, &at, &end);
        pl[2] = 1;
        for (i = 0; i < 4; i++) { pl[3 + i] = (uint8_t)(at >> (8 * i)); pl[7 + i] = (uint8_t)(end >> (8 * i)); }
        pl[11] = (uint8_t)got; n = 12 + got;
    } else if (proto_req[1] == 0x0F && proto_req_len == 2) {
        /* the controls (platform 0.5.2): 7E 0F -> 7E 0F 1 <volume knob 0..255> <foot switch 0/1> <PARA pushed 0/1>
         * (the knob as filtered for fw_knob; the switches as read now, not debounced) */
        uint32_t a = gp5_reg_get(GP5_GPIO_A_IN), b = gp5_reg_get(GP5_GPIO_B_IN);
        pl[2] = 1; pl[3] = (uint8_t)knob_value(); pl[4] = !(a & FOOT_PIN); pl[5] = !(b & PARA_PIN); n = 6;
    } else if (proto_req[1] == 0x10 && proto_req_len == 8) {
        /* a setting of the running firmware, live (platform 0.5.4): 7E 10 <id u16> <value u32> -> 7E 10 <1 if taken>
         * (fw_param_set; firmwares without it answer 0) */
        uint32_t id = proto_req[2] | (proto_req[3] << 8);
        uint32_t v = proto_req[4] | (proto_req[5] << 8) | ((uint32_t)proto_req[6] << 16) | ((uint32_t)proto_req[7] << 24);
#ifdef GP5_HAS_PARAMS                               /* ids past the parameters: the firmware's own (fw_param_set) */
        pl[2] = (uint8_t)((id < GP5_NPARAMS ? gp5_params_set_outside(id, (int32_t)v) : fw_param_set(id, v)) != 0); n = 3;
#else
        pl[2] = (uint8_t)(fw_param_set(id, v) != 0); n = 3;
#endif
    } else if (proto_req[1] == 0x11 && proto_req_len == 5) {
        /* read settings (0.5.4): 7E 11 <first id u16> <count, at most 40> -> 7E 11 <n> <n values, u16 each>; n stops
         * at the first id the firmware doesn't know (fw_param_get) */
        uint32_t first = proto_req[2] | (proto_req[3] << 8), v;
        unsigned cnt = proto_req[4] > 40u ? 40u : proto_req[4];
        pl[2] = 0; n = 3;
#ifdef GP5_HAS_PARAMS
        for (i = 0; i < cnt; i++) {
            if (first + i < GP5_NPARAMS) v = (uint32_t)gp5_param(first + i);
            else if (first < GP5_NPARAMS || !fw_param_get(first + i, &v)) break;   /* the firmware's own ids, from 1.0 */
            pl[n++] = (uint8_t)v; pl[n++] = (uint8_t)(v >> 8); pl[2]++;
        }
#else
        for (i = 0; i < cnt && fw_param_get(first + i, &v); i++) { pl[n++] = (uint8_t)v; pl[n++] = (uint8_t)(v >> 8); pl[2]++; }
#endif
    } else if (proto_req[1] == 0x0E && proto_req_len == 6) {
        /* the computer changed a store record: 7E 0E <firmware id u32> -> 7E 0E <1 if the firmware reloaded it> */
        uint32_t id = proto_req[2] | (proto_req[3] << 8) | ((uint32_t)proto_req[4] << 16) | ((uint32_t)proto_req[5] << 24);
#ifdef GP5_HAS_PARAMS
        if (gp5_params_store_changed(id)) { (void)api_store_changed(id); pl[2] = 1; n = 3; } else
#endif
        { pl[2] = (uint8_t)(api_store_changed(id) != 0); n = 3; }
    } else return;
    proto_reply(0x01, 0, pl, n);
}

/* ---- the firmware's callbacks (OpenGP5 1.0) ---- */
static unsigned api_knob;
unsigned gp5_knob(void) { return api_knob; }
static unsigned api_level, api_level_owned;
static uint32_t api_led = 1u;                       /* GP5_LED_ON */
void gp5_level_own(int own) { api_level_owned = own != 0; }
void gp5_level_set(unsigned level) { api_level = level > 255u ? 255u : level; }
void gp5_led(uint32_t colour) { api_led = colour; }
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
__attribute__((weak)) void fw_midi(const uint8_t *msg, unsigned n) { (void)msg; (void)n; }
#ifdef GP5_BLUETOOTH
void gp5_bt_start(void);                         /* bt/gp5_bt.c */
#endif
__attribute__((weak)) int fw_take(uint32_t *id, uint32_t *value) { (void)id; (void)value; return 0; }
__attribute__((weak)) unsigned fw_setting_count(void) { return 0; }
#define PARA_HOLD_MS 600u
static uint32_t para_t0; static int para_held;
static void api_tick(unsigned knob) { api_knob = knob; fw_tick(); }
static unsigned api_level_now(void) { return api_level_owned ? api_level : api_knob; }
static void api_foot(int down) { if (down) fw_foot_down(); else fw_foot_up(); }
static void api_para(int down)
{
    if (down) { para_t0 = gp5_ms(); para_held = 0; fw_para_down(); return; }
    if (!para_held) fw_para_click();
    para_held = 1;                                  /* (no hold after the release) */
    fw_para_up();
}
static void api_para_poll(int down) { if (down && !para_held && gp5_ms() - para_t0 >= PARA_HOLD_MS) { para_held = 1; fw_para_hold(); } }
static uint32_t api_led_now(void) { return api_led; }
static int api_draw(unsigned knob, int force) { (void)knob; return fw_draw(force); }
static int api_store_changed(uint32_t id) { (void)id; fw_storage_changed(); return 1; }

/* gp5_firmware.h: optional for firmwares */
const char *fw_name(void) { return GP5_FW_ID; }      /* the manifest's id */
void gp5_storage_claim(void);
__attribute__((weak)) int fw_param_get(uint32_t id, uint32_t *value) { (void)id; (void)value; return 0; }
__attribute__((weak)) int fw_param_set(uint32_t id, uint32_t value) { (void)id; (void)value; return 0; }

/* PARA encoder: quadrature on B2/B3, sampled wherever the code can spend time (main loop, screen rows) */
static int enc;
static unsigned enc_lab;
static void enc_poll(void)
{
    /* quadrature step table, index = (previous AB << 2) | current AB */
    static const int8_t qstep[16] = { 0,1,-1,0, -1,0,0,1, 1,0,0,-1, 0,-1,1,0 };
    unsigned ab = (gp5_reg_get(GP5_GPIO_B_IN) >> 2) & 3;
    enc += qstep[(enc_lab << 2) | ab];
    enc_lab = ab;
}

/* gp5_firmware.h: pixels to the screen, row by row (the PARA encoder is sampled between rows) */
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px)
{
    unsigned j, r, c;
    for (r = 0; r < ROWS; r++)                      /* text cells under the pixels must be redrawn next time */
        for (c = 0; c < COLS; c++)
            if (r * CH < y + h && y < r * CH + CH && 2 + c * CW < x + w && x < 2 + c * CW + CW) shown[r][c] = 0;
    set_window(x, x + w - 1, y, y + h - 1);
    for (j = 0; j < h; j++) {
        lcd_dat((uint8_t *)(px + j * w), w * 2);
        enc_poll();
    }
}

void gp5_text(unsigned line, const char *s)
{
    if (line < ROWS) put_line(line, s, rgb(255,255,255), rgb(0,0,0));
}

#define STATUS_AFTER_MS 4000u

/* black screen, and forget what text was on it */
static void clear_screen(void)
{
    fill_rect(0, 0, W, H, rgb(0, 0, 0));
    __builtin_memset(shown, 0, sizeof shown); __builtin_memset(shown_col, 0, sizeof shown_col);
}

static void monitor(void)
{
    uint16_t white = rgb(255,255,255), black = rgb(0,0,0), yellow = rgb(255,255,0), green = rgb(0,255,0);
    uint32_t a, b;
    uint32_t rx = 0, usb = 0, lusb = ~0u;
    uint32_t dsp_ver = 0, dsp_next = 0, dsp_tries = 0;
    uint32_t knob_next = 0, fx_next = 0;
    uint32_t fx_startup_acked = 0, fx_all;  /* decorative LEDs wait for every initial setting */
    /* The sound stays muted (output level 0) until the DSP has every setting the firmware starts with: otherwise the
     * first moments play the DSP's defaults, or the guitar dry. Unmuted once nothing is left to send after the DSP's
     * start-up, or AUDIO_WAIT_MS after it at the latest; the DSP then fades in. */
    int audio_on = 0;
    uint32_t audio_wait = 0;
    unsigned knob = 0, lstep = ~0u, knob_reads = 0;
    int dsp_show = 0, host_asked, status = 0;
    uint32_t t_start;
    int det, ldet = 0, foot, para, lfoot = 0, lpara = 0, gfx = 0, gforce = 0;
    uint32_t foot_quiet = 0, para_quiet = 0;
    unsigned ab, n, i;
    static uint8_t mbuf[64];
    unsigned port;
    uint8_t asked_port = 0;
    char s[COLS + 1];
    char *p;

    /* The start-up logo stays up until the firmware's screen takes over. Only if that takes more than
     * STATUS_AFTER_MS (the DSP not answering, say) does the platform's status screen replace it. */
    t_start = gp5_ms();
    enc = 0;
    enc_lab = (gp5_reg_get(GP5_GPIO_B_IN) >> 2) & 3;
    __nds32__mtsr(1u, NDS32_SR_PFM_CTL);             /* performance counter 0 counts cycles (the load report) */
    __nds32__isb();
    gp5_storage_claim();                             /* gp5_files.c: storage belongs to this firmware (0.5) */
    gp5_log("OpenGP5 %s, firmware %s, control processor %u MHz, %u Hz", GP5_VERSION_TEXT, fw_name(), (unsigned)GP5_CTL_MHZ, (unsigned)GP5_SAMPLE_RATE);
#if defined(GP5_USB_AUDIO) && defined(GP5_DEBUG_PEEK)
    {   /* debug USB-audio builds: the crash record (noise after a power cut; a crash restarts, start/vectors.S) */
        extern volatile uint32_t gp5_crash[4];
        gp5_log("crash record %08X %08X %08X %08X", (unsigned)gp5_crash[0], (unsigned)gp5_crash[1], (unsigned)gp5_crash[2], (unsigned)gp5_crash[3]);
        gp5_crash[0] = 0xC1EA0000u;
    }
#endif
#ifdef GP5_HAS_PARAMS
    gp5_params_init();                               /* defaults, then what was saved */
    fw_init();
    fx_all = gp5_params_dsp_mask();
#else
    fw_init();
    n = fw_setting_count();
    fx_all = n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u;
#endif
#ifdef GP5_USB_AUDIO
    if (!gp5_link_running()) gp5_link_start(0);       /* USB audio travels in the link: a firmware without one gets it */
#endif

    for (;;) {
        {   static uint32_t c_prev;                  /* the main loop's longest pass (7E 14) */
            uint32_t c = __nds32__mfsr(NDS32_SR_PFMC0);
            if (c_prev && !loop_skip && c - c_prev > loop_worst && gp5_ms() > 5000u) loop_worst = c - c_prev;
            c_prev = c; loop_skip = 0;           /* (not the pass that answered 7E 14: it waited for the DSP) */
        }
        a = gp5_reg_get(GP5_GPIO_A_IN);
        b = gp5_reg_get(GP5_GPIO_B_IN);
        enc_poll();
        ab = enc_lab;

        usb_midi_poll();
        host_asked = 0;
        for (port = 0; port < 2u; port++) {          /* USB, then the second MIDI port (Bluetooth), if a firmware has one */
        n = port ? gp5_midi_port_read(mbuf, sizeof mbuf) : usb_midi_read(mbuf);
        proto_port = (uint8_t)port;
        for (i = 0; i + 4 <= n; i += 4) {
            int r;
            if (mbuf[i] & 0x0F) rx = ((uint32_t)mbuf[i + 1] << 16) | ((uint32_t)mbuf[i + 2] << 8) | mbuf[i + 3];
            {   /* notes, controllers, program changes (USB-MIDI code index 8..E), and real-time bytes (clock, start,
                 * continue, stop: code index F, one byte from 0xF8): to the firmware (1.0) */
                unsigned cin = mbuf[i] & 0x0Fu;
                if (cin >= 0x8u && cin <= 0xEu) fw_midi(mbuf + i + 1, cin == 0xCu || cin == 0xDu ? 2u : 3u);
                else if (cin == 0xFu && mbuf[i + 1] >= 0xF8u) fw_midi(mbuf + i + 1, 1u);
            }
            r = proto_feed(mbuf + i);
            if (r == PROTO_TO_LOADER) {
                put_line(4, "TO LOADER", yellow, black);
                /* The DSP goes quiet first (its firmware's processing paused, plain passthrough): the loader talks
                 * to the DSP as it starts, and a busy DSP, which reads its serial port only between audio blocks,
                 * could miss that and leave the loader waiting. Sent twice in case a busy DSP misses the first. */
                dsp_link_send(0x7F05, 0, 0);
                delay_ms(5);
                dsp_link_send(0x7F05, 0, 0);
                /* Always: restart the chip with the hand-over mark set; the next start sees it and hands over before
                 * anything else runs. After the audio link has run, the factory loader crashes once the host connects
                 * (cause not found, docs/platform/protocols.md); and handing over from here, with USB, the DSP link
                 * and the timers live, failed now and then for firmwares without the link too (1.0, the tremolo:
                 * the pedal came back in the app). The early hand-over has worked every time. */
                delay_ms(100);                   /* as stock; the reply is already with the host */
                if (gp5_link_running()) gp5_link_stop();
                HANDOVER_MARK[0] = HANDOVER_MAGIC; HANDOVER_MARK[1] = ~HANDOVER_MAGIC;
                __nds32__gie_dis();
                *(volatile uint32_t *)0x40022008u = 1;   /* system reset (the vendor's Reset_McuSystem) */
                for (;;) ;
            }
            if (r == PROTO_DSP) {
                if (proto_req[1] == 0x00) { host_asked = 1; asked_port = (uint8_t)port; }
                else host_dsp_cmd();
            }
        }
        }
        proto_port = 0;

        dsp_link_poll();
        gp5_link_check();                            /* the audio link restarts itself if it stalls */

        /* DSP link: ask for the DSP's version once a second until it answers, and whenever the host asks.
         * Each query holds the loop for at most 30 ms. */
        if (host_asked || (!dsp_ver && (int32_t)(gp5_ms() - dsp_next) >= 0)) {
            uint32_t v = dsp_link_version();
            if (v) dsp_ver = v;
            dsp_tries++;
            dsp_next = gp5_ms() + 1000;
            dsp_show = 1;
            if (host_asked) {
                uint8_t pl[7];
                pl[0] = 0x7E; pl[1] = 0x00; pl[2] = v ? 1 : 0;
                pl[3] = (uint8_t)v; pl[4] = (uint8_t)(v >> 8); pl[5] = (uint8_t)(v >> 16); pl[6] = (uint8_t)(v >> 24);
                proto_port = asked_port;
                proto_reply(0x01, 0, pl, 7);
                proto_port = 0;
            }
        }
        /* volume knob every 10 ms; start-up sync, then knob changes, to the DSP */
        if ((int32_t)(gp5_ms() - knob_next) >= 0) {
            knob_poll();
            knob = knob_value();
            knob_next = gp5_ms() + 10;
            /* the first readings ramp up from 0 (filter start): not a turn of the knob */
            if (knob_reads < 60) knob_reads++; else api_tick(knob);
        }
#ifdef GP5_USB_AUDIO
        usb_audio_poll();
#endif
        dsp_ctl_poll(dsp_ver != 0, audio_on ? api_level_now() : 0u);
        if (dsp_ctl_step != lstep) {
            lstep = dsp_ctl_step; dsp_show = 1;
            if (dsp_ctl_step == DSP_CTL_FAILED) gp5_log("DSP: start-up sync failed");
            if (dsp_ctl_step == DSP_CTL_DONE) {          /* the DSP starts with its own defaults */
                gp5_log("DSP ready, version %08lx", (unsigned long)dsp_ver);
#ifdef GP5_HAS_PARAMS
                gp5_params_dsp_restart();
#endif
                fw_dsp_ready();
                audio_wait = gp5_ms() + AUDIO_WAIT_MS;
            }
        }
        /* effect settings to the DSP: one per pass (each waits for its ACK), then at most one every 20 ms; while the
         * sound is muted at start-up, back to back */
        if (dsp_ctl_step == DSP_CTL_DONE && (!audio_on || (int32_t)(gp5_ms() - fx_next) >= 0)) {
            uint32_t id, val;
#ifdef GP5_HAS_PARAMS
            if (gp5_params_take(&id, &val) || fw_take(&id, &val)) {
#else
            if (fw_take(&id, &val)) {
#endif
                uint8_t pl[12], rep[8], st;
                pl[0] = 0x10; pl[1] = 0x7F; pl[2] = 12; pl[3] = 0;
                for (i = 0; i < 4; i++) { pl[4 + i] = (uint8_t)(id >> (8 * i)); pl[8 + i] = (uint8_t)(val >> (8 * i)); }
                dsp_link_raw(pl, sizeof pl, rep, sizeof rep, 100, &st);
                if (st == 0x14 && id < 32) fx_startup_acked |= 1u << id;
                fx_next = gp5_ms() + 20;
            } else if (!audio_on) {
                audio_on = 1;                            /* every start-up setting is with the DSP: sound on */
                if (gp5_link_running()) gp5_log("sound on (audio link: %u exchanges, %u lost)", (unsigned)gp5_link_blocks, (unsigned)gp5_link_lost);
                else gp5_log("sound on");
            }
            if (!audio_on && (int32_t)(gp5_ms() - audio_wait) >= 0) {
                audio_on = 1;
                gp5_log("sound on after %u ms: the firmware still had settings to send", (unsigned)AUDIO_WAIT_MS);
            }
        }

#ifdef GP5_HAS_PARAMS
        gp5_params_poll();                               /* saves the sound 2 s after the last change */
#endif
        if (!gfx && !status && (int32_t)(gp5_ms() - t_start) >= STATUS_AFTER_MS) {
            clear_screen();
            put_line(0, "OPENGP5 " GP5_VERSION_TEXT, yellow, black);
            status = 1; dsp_show = 1; lusb = ~0u;
        }
        if (dsp_show && !gfx && status) {
            /* answered: "D V01010006 n" (n = sync step, K = done, X = gave up); not answered: "D" +
             * tries + bytes received so far */
            p = s; *p++ = 'D'; *p++ = ' ';
            if (dsp_ver) {
                *p++ = 'V'; p = hex(p, dsp_ver, 8); *p++ = ' ';
                *p++ = dsp_ctl_step == DSP_CTL_DONE ? 'K' : dsp_ctl_step == DSP_CTL_FAILED ? 'X' : (char)('0' + dsp_ctl_step);
            }
            else { p = hex(p, dsp_tries, 2); *p++ = ' '; p = hex(p, dsp_rx_bytes, 8); }
            *p = 0;
            put_line(1, s, dsp_ver ? green : yellow, black);
            dsp_show = 0;
        }

        /* Switch contacts bounce for a few ms: take a change at once, then ignore the switch for DEBOUNCE_MS so
         * the bounce cannot add a second press (or cancel the first); a change that outlasts it is taken then. */
        foot = !(a & FOOT_PIN); para = !(b & PARA_PIN);
        if (foot != lfoot && (int32_t)(gp5_ms() - foot_quiet) >= 0) {
            midi_send(foot ? 0x90 : 0x80, 60, foot ? 100 : 0);
            api_foot(foot);
            lfoot = foot;
            foot_quiet = gp5_ms() + DEBOUNCE_MS;
        }
        if (para != lpara && (int32_t)(gp5_ms() - para_quiet) >= 0) {
            midi_send(para ? 0x90 : 0x80, 62, para ? 100 : 0);
            api_para(para);
            lpara = para;
            para_quiet = gp5_ms() + DEBOUNCE_MS;
        }
        api_para_poll(lpara);
        det = enc >> 1;                              /* a detent every 2 quadrature steps: at 11 and at 00 */
        if ((ab == 3 || ab == 0) && det != ldet) {
            midi_send(0xB0, 16, (uint8_t)(det & 0x7F));
            fw_para_turn(det - ldet);
            ldet = det;
        }

        led_set_effect(api_led_now());

        /* The graphic effect screen takes over once the DSP is in sync (one band per pass); until then,
         * and for good if the sync fails, the text lines above and the USB line below stay. */
        if (!gfx && dsp_ctl_step == DSP_CTL_DONE) { gfx = 1; gforce = 1; clear_screen(); }
        if (gfx) {
            if (!api_draw(knob, gforce) && (fx_startup_acked & fx_all) == fx_all) led_start();
            gforce = 0;
        } else if (status) {
            usb = ((uint32_t)usb_midi_configured() << 31) ^ (usb_setups << 24) ^ rx;
            if (usb != lusb) {
                p = s; *p++ = 'U'; *p++ = usb_midi_configured() ? '1' : '0'; *p++ = ' ';
                p = hex(p, usb_setups, 3); *p++ = ' '; p = hex(p, rx, 6); *p = 0;
                put_line(4, s, white, black);
                lusb = usb;
            }
        }
    }
}

int main(void)
{
    __nds32__gie_dis();
    gp5_stage = 1;

    /* chip: same calls and order as the SDK's Example_GPIO (stock uses PLL 320 MHz + LDO trim;
     * 288 MHz is the SDK's no-voltage-change setting) */
    gp5_chip_init();
    gp5_wdg_disable();
    gp5_clock_init();                            /* PLL GP5_CTL_MHZ system clock, APLL 240 MHz for USB + DSP link UART */
    gp5_stage = 2;

    board_init();
    io_init();
    if (HANDOVER_MARK[0] == HANDOVER_MAGIC && HANDOVER_MARK[1] == ~HANDOVER_MAGIC) {
        HANDOVER_MARK[0] = 0; HANDOVER_MARK[1] = 0;      /* once only */
        loader_handover();                       /* the host asked for update mode before this restart */
    }
    gp5_stage = 3;

    gp5_spim_clear_int();
    gp5_spim_init();
    gp5_stage = 4;

    panel_init();
    gp5_stage = 5;

    draw_logo();
    gp5_stage = 6;

    gp5_reg_bits_set(GP5_GPIO_A_OUT, GP5_PIN(15));   /* 0x47fa4 */
    backlight(0);                                /* the logo fades in: about 0.6 s, eased for the eye */
    for (unsigned n = 1; n <= 50; n++) {
        gp5_backlight_duty(n * n / 25);
        delay_ms(12);
    }
    gp5_stage = 7;

#ifndef GP5_BLUETOOTH
    delay_ms(900);                               /* 1.5 s in all, as before */
#endif

    __nds32__mtsr(0, NDS32_SR_INT_MASK2);        /* nothing enabled but what we enable below, whatever the loader left */
    gp5_tick_setup();                            /* timer 1, 1 ms (hal/tick.c) */
    GIE_ENABLE();
    NVIC_EnableIRQ(0);                           /* the timer-1 interrupt line */
#ifdef GP5_BLUETOOTH
    /* Bluetooth's start-up (system registers, the radio's calibration: ~1.1 s) while the logo shows, before the DSP
     * link and the audio link exist. Started after 'sound on' (as until 2026-11-23) it sometimes stopped the audio
     * link for good: its writes to the 0x400300xx system registers while an SPI-slave/DMA exchange ran (16c, 16d:
     * the link stopped right then, its restart couldn't bring it back; other starts were fine). */
    {
        uint32_t t = gp5_ms();
        gp5_bt_start();
        while (gp5_ms() - t < 900u) { }          /* the logo's 1.5 s in all, as without Bluetooth */
    }
#endif
    gp5_stage = 8;
    usb_midi_init();                             /* attaches to the host (D+ pull-up) */
    dsp_link_init();
    knob_init();
    gp5_stage = 9;
    monitor();
    return 0;
}
