/* GP-5 open platform, control processor side: what a firmware implements and what it may use.
 *
 * A firmware (a folder with a firmware.json, docs/platform/api.md) supplies the C files in its nds32/ folder (all are
 * compiled in) and defines the fw_* functions below that it needs. The platform owns start-up, the display driver, the inputs,
 * the LEDs, USB-MIDI, the DSP link and the hand-over to the factory loader; it calls the firmware
 * from its main loop, so none of these may block.
 *
 * Start-up: the platform shows its own text lines until the DSP answers and is in sync, then calls
 * fw_dsp_ready() and from then on fw_draw() every pass of the main loop. */
#ifndef GP5_FIRMWARE_H
#define GP5_FIRMWARE_H
#include <stdint.h>

#define GP5_VERSION_MAJOR 1           /* the platform's version (shown at start-up, told to the computer by 7E 0D) */
#define GP5_VERSION_MINOR 0
#define GP5_VERSION_PATCH 0
#define GP5_VERSION_TEXT "1.0.0"

/* Something the platform will retire (in a later major version only): using it warns when you build, with what to use
 * instead. 1.x keeps it working; a deprecation warns for at least one whole minor release before 2.0 removes it. */
#ifndef GP5_DEPRECATED
#define GP5_DEPRECATED(instead) __attribute__((deprecated(instead)))
#endif

#define GP5_SCREEN_W 160
#define GP5_SCREEN_H 80

/* ---- implemented by the firmware ---- */
/* Every callback is optional: the platform has an empty default for each. All run in the main loop, so none of them may wait. */
void fw_init(void);                       /* once, before anything else (the DSP may not be ready yet) */
int fw_draw(int force);                   /* draw (part of) the screen; return non-zero while more is left for the
                                             next pass. force = redraw everything (the first call, for one) */
void fw_tick(void);                       /* every 10 ms: the clock for timing things (gp5_ms(), gp5_knob()) */
void fw_foot_down(void);                  /* the foot switch pressed / let go (debounced by the platform, 30 ms) */
void fw_foot_up(void);
/* MIDI from the computer (USB) or over Bluetooth (firmware.json "bluetooth": true), 1.0: one message at a time, its
 * status byte first, from the main loop. A note, controller, program change, pressure or pitch bend (n = 2 or 3), or a
 * real-time byte (n = 1): clock 0xF8 (24 a beat), start 0xFA, continue 0xFB, stop 0xFC, active sensing 0xFE, reset
 * 0xFF. Optional. SysEx is the platform's (its command protocol). */
void fw_midi(const uint8_t *msg, unsigned n);
void fw_para_down(void);                  /* the PARA knob pushed / let go */
void fw_para_up(void);
void fw_para_click(void);                 /* ... a push let go before 0.6 s (called just before fw_para_up) */
void fw_para_hold(void);                  /* ... held for 0.6 s (called once while it is still held; no click follows) */
void fw_para_turn(int detents);           /* PARA turned, +/- clicks */
void fw_storage_changed(void);            /* the computer changed this firmware's files or store records: read them again */
/* A firmware without params.h sends settings to the DSP through fw_take / fw_setting_count / fw_dsp_ready and gives
 * the computer live access through fw_param_get / fw_param_set (below; all optional). With params.h the platform does
 * both (gp5_params.h); fw_param_get / fw_param_set then answer only ids from GP5_NPARAMS up: a firmware's own extra
 * live ids (Dreamcatcher's preset commands, for one). */
void fw_dsp_ready(void);
int fw_take(uint32_t *id, uint32_t *value);
unsigned fw_setting_count(void);
int fw_param_get(uint32_t id, uint32_t *value);
int fw_param_set(uint32_t id, uint32_t value);

/* ---- provided by the platform (1.0) ---- */
unsigned gp5_knob(void);                  /* the volume knob, 0..255 (filtered) */
void gp5_level_own(int own);              /* 1: the firmware sets the output level (gp5_level_set); 0 (the default):
                                             the volume knob is the output level */
void gp5_level_set(unsigned level);       /* output level 0..255, when owned */
void gp5_led(uint32_t colour);            /* the LEDs: GP5_LED_BYPASS, GP5_LED_ON or GP5_LED_RGB(r, g, b) */
#define GP5_LED_BYPASS 0u                 /* dim red */
#define GP5_LED_ON     1u                 /* mint (the default) */
#define GP5_LED_RGB(r, g, b) GP5_LED(r, g, b)
const char *fw_name(void);                /* (the platform's: the manifest's id) */

/* A colour for gp5_led() (GP5_LED_RGB is the same): r, g, b = brightness 0..32 (32 = fully lit) on each of
 * the LEDs' three colour lines. LEDs are not linear and their colours differ in strength: check on the pedal. */
#define GP5_LED(r, g, b) (0x01000000u | (uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

/* ---- provided by the platform ---- */
/* A line for the log (1.0): printf-style (%d %u %x %s %c %f, width and precision: platform/dsp/gp5_fmt.h), at most
 * about 110 characters, with the time since start in front. A computer reads it: gp5_link.py log, Launchpad's
 * console. The DSP has the same gp5_log (gp5_dsp.h); its lines arrive here marked "dsp: ". uint32_t is an unsigned
 * long on both processors: print it with %lu / %lx (the compiler checks the format). */
void gp5_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t gp5_ms(void);                    /* milliseconds since start */
/* The core's cycle counter now, for timing a piece of code: c = GP5_CYCLE_COUNT(); ...; c = GP5_CYCLE_COUNT() - c.
 * (288 or 320 million a second; on the computer it reads 0.) */
#if defined(__NDS32__)
#include <nds32_intrinsic.h>
#define GP5_CYCLE_COUNT() ((uint32_t)__nds32__mfsr(NDS32_SR_PFMC0))
#else
#define GP5_CYCLE_COUNT() 0u
#endif
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
unsigned gp5_store_load(uint32_t id, void *dst, unsigned max);
/* Files (platform 0.4, docs/platform/storage.md): bigger things a firmware keeps (captures, samples, IRs), owned by
 * a firmware id, named (up to 31 ASCII characters), written by the computer (OpenGP5 Launchpad). Read them straight
 * from flash: gp5_file() returns a pointer to the data (len set), or 0 if there is no such file. It checks the
 * header only; gp5_file_check() also checks the data's CRC (a few ms for 100 KB). gp5_file_each() calls fn for
 * every file of `firm` (newest copy of each name). When the computer changes your files or records,
 * fw_storage_changed() is called (see above). */
const uint8_t *gp5_file(uint32_t firm, const char *name, uint32_t *len);
int gp5_file_check(uint32_t firm, const char *name);              /* 1 = present and intact */
unsigned gp5_file_each(uint32_t firm, void (*fn)(const char *name, const uint8_t *data, uint32_t len, void *ctx), void *ctx);
int gp5_store_save(uint32_t id, const void *src, unsigned len);    /* 0 = saved and read back */
/* Maths: this processor has no C library either. gp5_math.h (platform/dsp, on the include path) declares the
 * platform's sinf, cosf, tanf, expf, exp2f, logf, log2f, ...; one file of the firmware includes "gp5_math.c" to
 * build them. The same code then runs on both processors (e.g. Dreamcatcher's engine on this one). */

/* Audio link to the DSP (platform/nds32/audio_link.c, docs/platform/audio-link.md): with "audio": {"link": true} in
 * firmware.json both processors exchange GP5_LINK_WORDS words after every audio block. Start it once (e.g. in
 * fw_init); on_block() then runs in the link's interrupt once per block (1.45 ms) with the words just received
 * and the words to fill for the DSP: keep it shorter than a block. What the DSP sends comes back to it, processed
 * here, 3 blocks later. gp5_link_blocks counts the exchanges. GP5_LINK_WORDS comes from firmware.json
 * ("audio": {"link_words": N}), else 128. */
#ifndef GP5_LINK_WORDS
#define GP5_LINK_WORDS 128u
#endif
/* The audio's sample rate: 44100, or 48000 when firmware.json says "audio": {"sample_rate": 48000}
 * One block (on_block) is 64 samples: 1.45 ms or 1.33 ms. */
#ifndef GP5_SAMPLE_RATE
#define GP5_SAMPLE_RATE 44100u
#endif
/* The control processor's clock in MHz: 288, or 320 (Valeton's own setting, with the core at 1.25 V) when firmware.json
 * says "control": {"mhz": 320}: 11 % more cycles per block. */
#ifndef GP5_CTL_MHZ
#define GP5_CTL_MHZ 288u
#endif
#define GP5_BLOCK 64u                     /* samples in one audio block (as on the DSP) */
void gp5_link_start(void (*on_block)(const int32_t *in, int32_t *out));
extern volatile uint32_t gp5_link_blocks;
/* USB audio (OpenGP5 1.0): the pedal is a 2-in / 2-out USB audio device unless firmware.json says "usb_audio": false.
 * The computer's playback is mixed into the outputs on the DSP; it records the firmware's finished output. With
 * "usb_audio": {"record_from": "control"} (a firmware that finishes its sound here) the firmware hands that output over
 * from its on_block(), once a block: 64 samples each side, full scale +-2^31 (a block it doesn't hand over is
 * recorded as silence). The platform's 64 link words follow the
 * firmware's own (GP5_LINK_WORDS): a firmware has at most 200 words in a direction it shares with USB audio. */
#ifdef GP5_USB_RECORD_CONTROL
void gp5_usb_audio_record(const int32_t *left, const int32_t *right);
#endif
#ifdef GP5_USB_AUDIO
/* The pedal's own sound at its outputs while the computer plays into the pedal: on (1, the default) or off (0), so a DAW
 * that monitors the pedal isn't heard twice. Recording is unchanged; with nothing playing, the pedal is always heard.
 * The computer sets it too (7E 19); it isn't kept over a restart unless the firmware stores it. */
void gp5_usb_audio_set_direct(int on);
int gp5_usb_audio_direct(void);
#endif
#endif
