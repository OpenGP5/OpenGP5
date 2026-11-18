/* GP-5 open platform, DSP side: what a firmware implements and what it may use.
 *
 * A firmware is one header, firmwares/<name>/dsp/firmware.h (it may include more headers of its own),
 * compiled into the platform's DSP program. It defines the three functions declared below. Write ordinary
 * C: static and global variables work (initialised ones too), and so does float arithmetic on the DSP's
 * floating-point unit. For maths functions include "gp5_math.h" (sinf, cosf, expf, logf, powf, tanhf, ...;
 * the DSP has no C library). Use float, not double: write 0.5f, not 0.5.
 *
 * Audio: mono in (the guitar, 32-bit signed, left-justified: full scale = +-2^31), stereo out, GP5_BLOCK
 * frames per call, GP5_SAMPLE_RATE Hz: 44100, or 48000 when firmware.json says "audio": {"sample_rate": 48000}
 * (both processors get it; 0.5.6). The platform applies the output level (the volume knob) after fw_process(). One
 * block is 1.45 ms at 44.1 kHz, 1.33 ms at 48 kHz; the core runs at about 191.9 MHz: 278,000 cycles per block
 * (255,900 at 48 kHz). The
 * platform counts cycles per block (GP5_CYCLES_*) and blocks that overran (GP5_LATE_BLOCKS).
 *
 * Commands: the control processor sends link commands 0x7F10..0x7FFF to fw_command() (payload
 * [cmd u16][len u16][fields]). Answer with gp5_ack() or gp5_reply() and return 1; return 0 for a
 * command you do not know (the platform then acknowledges it).
 *
 * Memory:
 *   variables (static / global)         19.5 KB   fast RAM (the stack is separate: 4 KB)
 *   buffers tagged GP5_LARGE            122 KB    delay lines, loops: static int16_t buf[48000] GP5_LARGE;
 * Both are zeroed (or set to their initial values) before fw_init(). The build prints how much is used and
 * fails if it does not fit. The platform keeps the rest (see platform/dsp/link.ld).
 *
 * Try an effect on the computer before flashing: python3 tools/run_effect.py <name> in.wav out.wav. */
#ifndef GP5_DSP_H
#define GP5_DSP_H
#include <stdint.h>

#define GP5_BLOCK       64u
#ifndef GP5_SAMPLE_RATE
#define GP5_SAMPLE_RATE 44100u
#endif
#define GP5_LARGE __attribute__((section(".large")))     /* put a big buffer in the 122 KB block */
#define GP5_FULL_SCALE 2147483648.0f                     /* in[] / GP5_FULL_SCALE = -1.0 .. 1.0 */

static void fw_init(void);
static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right);
static int  fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n);
/* Parameters (1.0, a firmware with params.h; gp5_params.h): the platform keeps every value (gp5_param(id), the
 * default until the control processor sends one) and calls fw_param() with each change, before the next
 * fw_process(). A firmware with parameters defines it (even an empty one). Link command 0x7F10 is the platform's
 * then; the firmware's own commands start at 0x7F11. */
#include "gp5_params.h"
#ifdef GP5_HAS_PARAMS
static void fw_param(uint32_t id, int32_t value);
#endif

static void     gp5_reply(const uint8_t *payload, uint32_t n);   /* one link frame; payload starts with [cmd][len] */
static void     gp5_ack(void);
static uint32_t gp5_get32(const uint8_t *p);
/* A line for the log (1.0): printf-style as on the control processor (gp5_fmt.h), at most 100 characters; it travels
 * over the DSP link and shows up there marked "dsp: " (gp5_link.py log). It costs a few thousand cycles: not in
 * every block. On the computer (run_effect.py) it prints to the terminal. */
static void gp5_log(const char *fmt, ...) __attribute__((format(printf, 1, 2), unused));

/* Audio link to the control processor: "audio": {"link": true} in firmware.json (before 1.0: #define GP5_AUDIO_LINK
 * in firmware.h). After every block the
 * platform exchanges GP5_LINK_WORDS 32-bit words each way with it (gp5_link_start() in gp5_firmware.h there).
 * In fw_process(), gp5_link_in() holds the words received in the last exchange and gp5_link_out() takes the
 * words for the next one. Words sent from here come back processed three blocks later at the earliest.
 * An exchange takes about 38 % of a block; a firmware whose fw_process() runs long should also
 * set "link_early": true (the exchange then starts at the start of each block; see platform/dsp/audio.h).
 * A firmware that needs more words per exchange sets e.g. "link_words": 264: both
 * sides are built with it (the exchange then takes longer: about 38 % of a block per 128 words). */
#ifndef GP5_LINK_WORDS
#define GP5_LINK_WORDS 128u
#endif
#ifdef AUDIO_SPI1                                     /* set by build.py when firmware.h defines GP5_AUDIO_LINK */
static const int32_t *gp5_link_in(void);
static int32_t *gp5_link_out(void);
#endif

#ifndef GP5_HOST
#define GP5_CYCLES_LAST  (*(volatile uint32_t *)0x20007F74u)     /* core cycles of the last audio block */
#define GP5_CYCLES_WORST (*(volatile uint32_t *)0x20007F78u)     /* worst block since boot */
#define GP5_LATE_BLOCKS  (*(volatile uint32_t *)0x20007F7Cu)     /* blocks that finished after the next one was ready */
#else                                                             /* tools/run_effect.py, on the computer */
static uint32_t GP5_CYCLES_LAST, GP5_CYCLES_WORST, GP5_LATE_BLOCKS;
static int32_t gp5_host_link[2][GP5_LINK_WORDS];                  /* no control processor here: words in stay 0 */
static __attribute__((unused)) const int32_t *gp5_link_in(void) { return gp5_host_link[0]; }
static __attribute__((unused)) int32_t *gp5_link_out(void) { return gp5_host_link[1]; }
#undef GP5_LARGE
#define GP5_LARGE
#endif
#endif
