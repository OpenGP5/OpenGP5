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

#define GP5_VERSION_MAJOR 0           /* the platform's version (shown at start-up, told to the computer by 7E 0D) */
#define GP5_VERSION_MINOR 6
#define GP5_VERSION_PATCH 0
#define GP5_VERSION_TEXT "0.6.0"

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
const char *fw_name(void);                /* optional: a short ASCII name (up to 32 characters), e.g. "dreamcatcher":
                                             OpenGP5 Launchpad asks for it (USB 7E 0D), and the storage belongs to it
                                             (0.5: a firmware with another name wipes it at start-up). Default "opengp5" */
int fw_store_changed(uint32_t id);        /* optional: the computer rewrote your store record `id` (7E 0E): load it
                                             again and return 1; return 0 if you can't (the user restarts the pedal) */
const char *fw_name(void);                /* optional: a short ASCII name (up to 32 characters), e.g. "dreamcatcher":
                                             OpenGP5 Launchpad asks for it (USB 7E 0D), and the storage belongs to it
                                             (0.5: a firmware with another name wipes it at start-up). Default "opengp5" */
int fw_store_changed(uint32_t id);        /* optional: the computer rewrote your store record `id` (7E 0E): load it again
                                             and return 1; return 0 if you can't (the user restarts the pedal) */
int fw_param_get(uint32_t id, uint32_t *value);   /* optional (0.5.4): live access from the computer (USB 7E 11 /
                                             7E 10): your settings by your own ids. get: 1 and *value if `id` is one */
int fw_param_set(uint32_t id, uint32_t value);    /* of yours; set: 1 if taken. Both run in the main loop, like fw_take,
                                             so they may change what fw_take hands to the DSP. Defaults: 0 (none). */

/* A firmware's own LED colour, returned by fw_active(): r, g, b = brightness 0..32 (32 = fully lit) on each of
 * the LEDs' three colour lines. LEDs are not linear and their colours differ in strength: check on the pedal. */
#define GP5_LED(r, g, b) (0x01000000u | (uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

/* ---- provided by the platform ---- */
/* Send w x h pixels (RGB565, big-endian bytes as the panel wants them) to the screen at x, y. */
void gp5_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *px);
/* One of five 13-character text lines in the built-in 5x7 pixel font, white on black. For smooth text in any font
 * and size, anywhere on the screen: gp5_font.h (gp5_text_at, gp5_draw_text) and docs/platform/fonts.md. */
void gp5_text(unsigned line, const char *s);

/* Storage that survives power-off (platform/nds32/gp5_store.c): up to GP5_STORE_MAX bytes, tagged with an id of
 * your choosing (e.g. four letters) so that another firmware's data is never taken for yours. Load returns the
 * length of the last save for that id (0 = nothing saved) and copies at most max bytes. A save takes about
 * 1 ms, and every few dozen saves up to a few hundred ms (a flash erase); the control processor does nothing
 * else meanwhile (the sound goes on: it is the DSP's). Only the platform's 8 KB store area is ever written. */
#define GP5_STORE_MAX 1024u
/* Data area: 56 KB of flash the firmware reads like memory (const pointer, e.g. a model or a sample), written from
 * the computer with tools/gp5_data.py while the firmware runs (flash writes pause it for a moment). The firmware
 * cannot write it. Erased flash reads 0xFF: check a magic word / checksum of your own before using the bytes. */
#define GP5_DATA      ((const uint8_t *)0x162000u)    /* (before 0.4; now part of the files: use gp5_file()) */
#define GP5_DATA_SIZE 0xE000u
unsigned gp5_store_load(uint32_t id, void *dst, unsigned max);
/* Files (platform 0.4, docs/platform/storage.md): bigger things a firmware keeps (captures, samples, IRs), owned by
 * a firmware id, named (up to 31 ASCII characters), written by the computer (OpenGP5 Launchpad). Read them straight
 * from flash: gp5_file() returns a pointer to the data (len set), or 0 if there is no such file. It checks the
 * header only; gp5_file_check() also checks the data's CRC (a few ms for 100 KB). gp5_file_each() calls fn for
 * every file of `firm` (newest copy of each name). When the computer changes your files it sends 7E 0E with your
 * id: fw_store_changed(id) is called (see above). */
const uint8_t *gp5_file(uint32_t firm, const char *name, uint32_t *len);
int gp5_file_check(uint32_t firm, const char *name);              /* 1 = present and intact */
unsigned gp5_file_each(uint32_t firm, void (*fn)(const char *name, const uint8_t *data, uint32_t len, void *ctx), void *ctx);
int gp5_store_save(uint32_t id, const void *src, unsigned len);    /* 0 = saved and read back */
/* Maths: this processor has no C library either. gp5_math.h (platform/dsp, on the include path) declares the
 * platform's sinf, cosf, tanf, expf, exp2f, logf, log2f, ...; one file of the firmware includes "gp5_math.c" to
 * build them. The same code then runs on both processors (e.g. Dreamcatcher's engine on this one). */

/* Audio link to the DSP (platform/nds32/audio_link.c). The DSP firmware opts in with #define GP5_AUDIO_LINK
 * (gp5_dsp.h) and exchanges GP5_LINK_WORDS words with this side after every audio block. Start it once (e.g. in
 * fw_init); on_block() then runs in the link's interrupt once per block (1.45 ms) with the words just received
 * and the words to fill for the DSP: keep it shorter than a block. Words sent here reach the DSP two exchanges
 * later. gp5_link_blocks counts the exchanges. GP5_LINK_WORDS comes from the DSP side's firmware.h when it sets
 * one (platform/nds32/build.sh passes it), else 128. */
#ifndef GP5_LINK_WORDS
#define GP5_LINK_WORDS 128u
#endif
/* The audio's sample rate: 44100, or 48000 when the DSP side's firmware.h sets #define GP5_SAMPLE_RATE 48000u
 * (0.5.6; platform/nds32/build.sh passes it). One block (on_block) is 64 samples: 1.45 ms or 1.33 ms. */
#ifndef GP5_SAMPLE_RATE
#define GP5_SAMPLE_RATE 44100u
#endif
/* The control processor's clock in MHz: 288, or 320 (Valeton's own setting, with the core at 1.25 V) when the DSP side's
 * firmware.h sets #define GP5_CTL_MHZ 320 (0.6; platform/nds32/build.sh passes it): 11 % more cycles per block. */
#ifndef GP5_CTL_MHZ
#define GP5_CTL_MHZ 288u
#endif
void gp5_link_start(void (*on_block)(const int32_t *in, int32_t *out));
extern volatile uint32_t gp5_link_blocks;
#endif
