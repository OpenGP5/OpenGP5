/* GP-5 open platform, control processor (BP1048 / NDS32): the main program.
 *
 * Start-up (start/) hands over to main(), which brings the chip up (hal/clock.c), sets the board's pins as
 * Valeton's app leaves them, starts the screen (ST7735S-type, 160 x 80, SPI master port 0), the backlight,
 * the LEDs, USB-MIDI (usb_midi.c) and the link to the DSP (dsp_link.c), then loops:
 *   - answers USB requests (proto.c: restart into update mode, DSP link bridge);
 *   - waits for the DSP's version answer, sends it the output level (dsp_ctl.c) and calls fw_dsp_ready();
 *   - reads the foot switch, the PARA knob (turn + push) and the big knob, and calls the firmware's
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
#include "start/irq.h"
#include "usb_midi.h"
#include "proto.h"
#include "dsp_link.h"
#include "dsp_ctl.h"
#include "gp5_firmware.h"
#include "led.h"


#define DC_PIN   GP5_PIN(7)    /* A7  low = command (the SPIM port-0 MISO pin, taken back as GPIO) */
#define CS_PIN   GP5_PIN(9)    /* A9  low = selected */
#define RST_PIN  GP5_PIN(16)   /* A16 low = reset */
#define LCD_PINS (DC_PIN | CS_PIN | RST_PIN)

#define W 160
#define H 80

/* Busy-loop delay. Sized for the 288 MHz PLL clock; if the PLL did not lock (12 MHz RC) it only
 * runs long, never short. */
#define ITERS_PER_MS 100000u
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
    0x3A, 1, 0x05,  0x36, 1, 0xA8,  0x28, 0,  0x29, 0,
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

/* colours are given as R,G,B; the panel is in BGR mode, so swap here */
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
    if (ch < 0x20 || ch > 0x5A) ch = '?';
    g = font[ch - 0x20];
    for (y = 0; y < CH; y++) {
        for (x = 0; x < CW; x++) {
            unsigned fx = x / 2, fy = y / 2;
            uint16_t c = (fx < 5 && fy < 7 && ((g[fx] >> fy) & 1)) ? fg : bg;
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

/* Host commands 7E 01 .. 7E 04 (proto.c); the request is in proto_req. */
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
    } else return;
    proto_reply(0x01, 0, pl, n);
}

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
    unsigned knob = 0, lstep = ~0u, knob_reads = 0;
    int dsp_show = 0, host_asked, status = 0;
    uint32_t t_start;
    int det, ldet = 0, foot, para, lfoot = 0, lpara = 0, gfx = 0, gforce = 0;
    unsigned ab, n, i;
    static uint8_t mbuf[64];
    char s[COLS + 1];
    char *p;

    /* The start-up logo stays up until the firmware's screen takes over. Only if that takes more than
     * STATUS_AFTER_MS (the DSP not answering, say) does the platform's status screen replace it. */
    t_start = gp5_ms();
    enc = 0;
    enc_lab = (gp5_reg_get(GP5_GPIO_B_IN) >> 2) & 3;
    fw_init();
    n = fw_setting_count();
    fx_all = n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u;

    for (;;) {
        a = gp5_reg_get(GP5_GPIO_A_IN);
        b = gp5_reg_get(GP5_GPIO_B_IN);
        enc_poll();
        ab = enc_lab;

        usb_midi_poll();
        n = usb_midi_read(mbuf);
        host_asked = 0;
        for (i = 0; i + 4 <= n; i += 4) {
            int r;
            if (mbuf[i] & 0x0F) rx = ((uint32_t)mbuf[i + 1] << 16) | ((uint32_t)mbuf[i + 2] << 8) | mbuf[i + 3];
            r = proto_feed(mbuf + i);
            if (r == PROTO_TO_LOADER) {
                put_line(4, "TO LOADER", yellow, black);
                delay_ms(100);                   /* as stock; the reply is already with the host */
                loader_handover();
            }
            if (r == PROTO_DSP) {
                if (proto_req[1] == 0x00) host_asked = 1;
                else host_dsp_cmd();
            }
        }

        dsp_link_poll();

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
                proto_reply(0x01, 0, pl, 7);
            }
        }
        /* volume knob every 10 ms; start-up sync, then knob changes, to the DSP */
        if ((int32_t)(gp5_ms() - knob_next) >= 0) {
            knob_poll();
            knob = knob_value();
            knob_next = gp5_ms() + 10;
            /* the first readings ramp up from 0 (filter start): not a turn of the knob */
            if (knob_reads < 60) knob_reads++; else fw_knob(knob);
        }
        dsp_ctl_poll(dsp_ver != 0, fw_level());
        if (dsp_ctl_step != lstep) {
            lstep = dsp_ctl_step; dsp_show = 1;
            if (dsp_ctl_step == DSP_CTL_DONE) fw_dsp_ready();   /* the DSP starts with its own defaults */
        }
        /* effect settings to the DSP: one per pass, at most one every 20 ms (each waits for its ACK) */
        if (dsp_ctl_step == DSP_CTL_DONE && (int32_t)(gp5_ms() - fx_next) >= 0) {
            uint32_t id, val;
            if (fw_take(&id, &val)) {
                uint8_t pl[12], rep[8], st;
                pl[0] = 0x10; pl[1] = 0x7F; pl[2] = 12; pl[3] = 0;
                for (i = 0; i < 4; i++) { pl[4 + i] = (uint8_t)(id >> (8 * i)); pl[8 + i] = (uint8_t)(val >> (8 * i)); }
                dsp_link_raw(pl, sizeof pl, rep, sizeof rep, 100, &st);
                if (st == 0x14 && id < 32) fx_startup_acked |= 1u << id;
                fx_next = gp5_ms() + 20;
            }
        }

        if (!gfx && !status && (int32_t)(gp5_ms() - t_start) >= STATUS_AFTER_MS) {
            clear_screen();
            put_line(0, "GP5 OPEN FW28", yellow, black);
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

        foot = !(a & FOOT_PIN); para = !(b & PARA_PIN);
        if (foot != lfoot) {
            midi_send(foot ? 0x90 : 0x80, 60, foot ? 100 : 0);
            if (foot) fw_foot();
            lfoot = foot;
        }
        if (para != lpara) {
            midi_send(para ? 0x90 : 0x80, 62, para ? 100 : 0);
            if (para) fw_para_push();
            lpara = para;
        }
        det = enc >> 1;                              /* a detent every 2 quadrature steps: at 11 and at 00 */
        if ((ab == 3 || ab == 0) && det != ldet) {
            midi_send(0xB0, 16, (uint8_t)(det & 0x7F));
            fw_para_turn(det - ldet);
            ldet = det;
        }

        led_set_effect(fw_active());

        /* The graphic effect screen takes over once the DSP is in sync (one band per pass); until then,
         * and for good if the sync fails, the text lines above and the USB line below stay. */
        if (!gfx && dsp_ctl_step == DSP_CTL_DONE) { gfx = 1; gforce = 1; clear_screen(); }
        if (gfx) {
            if (!fw_draw(knob, gforce) && (fx_startup_acked & fx_all) == fx_all) led_start();
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
    gp5_clock_init();                            /* PLL 288 MHz system clock, APLL 240 MHz for USB + DSP link UART */
    gp5_stage = 2;

    board_init();
    io_init();
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

    delay_ms(900);                               /* 1.5 s in all, as before */

    __nds32__mtsr(0, NDS32_SR_INT_MASK2);        /* nothing enabled but what we enable below, whatever the loader left */
    gp5_tick_setup();                            /* timer 1, 1 ms (hal/tick.c) */
    GIE_ENABLE();
    NVIC_EnableIRQ(0);                           /* the timer-1 interrupt line */
    gp5_stage = 8;
    usb_midi_init();                             /* attaches to the host (D+ pull-up) */
    dsp_link_init();
    knob_init();
    gp5_stage = 9;
    monitor();
    return 0;
}
