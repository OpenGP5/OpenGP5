/* GP-5 open platform, control processor side: what a firmware implements and what it may use.
 *
 * A firmware supplies the C files in firmwares/<name>/nds32/ (all are compiled in). One of them
 * defines the fw_* functions below. The platform owns start-up, the display driver, the inputs,
 * the LEDs, USB-MIDI, the DSP link and the hand-over to the factory loader; it calls the firmware
 * from its main loop, so none of these may block.
 *
 * Start-up: the platform shows its own text lines until the DSP answers and is in sync, then calls
 * fw_dsp_ready() and from then on fw_draw() every pass of the main loop. */
#ifndef GP5_FIRMWARE_H
#define GP5_FIRMWARE_H
#include <stdint.h>

#define GP5_SCREEN_W 160
#define GP5_SCREEN_H 80

/* ---- implemented by the firmware ---- */
void fw_init(void);
void fw_foot(void);                       /* foot switch pressed */
void fw_foot_release(void);               /* optional: foot switch let go (empty default) */
void fw_para_push(void);                  /* PARA knob pushed */
void fw_para_release(void);               /* optional: PARA knob let go (the platform has an empty default).
                                             Time a hold with fw_knob's 10 ms. Foot switch and PARA push are
                                             debounced by the platform (30 ms). */
void fw_para_turn(int detents);           /* PARA knob turned, +/- clicks */
void fw_knob(unsigned knob);              /* volume knob, 0..255, about every 10 ms */
unsigned fw_level(void);                  /* output level for the DSP, 0..255 */
void fw_dsp_ready(void);                  /* the DSP (re)started with its defaults: queue every setting again */
int fw_take(uint32_t *id, uint32_t *value);   /* next setting for the DSP, sent as link command 0x7F10
                                                 [id u32][value u32]; return 0 when there is none */
unsigned fw_setting_count(void);          /* ids 0..count-1 (at most 32) must be acknowledged once before
                                             the platform starts the LEDs */
int fw_draw(unsigned knob, int force);    /* draw (part of) the screen; return non-zero while more is left
                                             for the next pass. force = redraw everything. */
unsigned fw_active(void);                 /* LED colour: 0 = dim red, 1 = mint, or GP5_LED(r, g, b) */

/* A firmware's own LED colour, returned by fw_active(): r, g, b = brightness 0..32 (32 = fully lit) on each of
 * the LEDs' three colour lines. LEDs are not linear and their colours differ in strength: check on the pedal. */
#define GP5_LED(r, g, b) (0x01000000u | (uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

/* ---- provided by the platform ---- */
/* Send w x h pixels (RGB565, big-endian bytes as the panel wants them) to the screen at x, y. */
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
/* One of five 13-character text lines in the platform's 5x7 font, white on black. */
void gp5_text(unsigned line, const char *s);

/* Storage that survives power-off (platform/nds32/gp5_store.c): up to GP5_STORE_MAX bytes, tagged with an id of
 * your choosing (e.g. four letters) so that another firmware's data is never taken for yours. Load returns the
 * length of the last save for that id (0 = nothing saved) and copies at most max bytes. A save takes about
 * 1 ms, and every few dozen saves up to a few hundred ms (a flash erase); the control processor does nothing
 * else meanwhile (the sound goes on: it is the DSP's). Only the platform's 8 KB store area is ever written. */
#define GP5_STORE_MAX 1024u
unsigned gp5_store_load(uint32_t id, void *dst, unsigned max);
int gp5_store_save(uint32_t id, const void *src, unsigned len);    /* 0 = saved and read back */
/* Audio link to the DSP (platform/nds32/audio_link.c). The DSP firmware opts in with #define GP5_AUDIO_LINK
 * (gp5_dsp.h) and exchanges GP5_LINK_WORDS words with this side after every audio block. Start it once (e.g. in
 * fw_init); on_block() then runs in the link's interrupt once per block (1.45 ms) with the words just received
 * and the words to fill for the DSP: keep it shorter than a block. Words sent here reach the DSP two exchanges
 * later. gp5_link_blocks counts the exchanges. */
#define GP5_LINK_WORDS 128u
void gp5_link_start(void (*on_block)(const int32_t *in, int32_t *out));
extern volatile uint32_t gp5_link_blocks;
#endif
