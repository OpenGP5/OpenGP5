/* GP-5 open platform, DSP side: what a firmware implements and what it may use.
 *
 * A firmware is one header, firmwares/<name>/dsp/firmware.h (it may include more headers of its own),
 * compiled into the platform's DSP program. It defines the three functions declared below. Write ordinary
 * C: static and global variables work (initialised ones too), and so does float arithmetic on the DSP's
 * floating-point unit. For maths functions include "gp5_math.h" (sinf, cosf, expf, logf, powf, tanhf, ...;
 * the DSP has no C library). Use float, not double: write 0.5f, not 0.5.
 *
 * Audio: mono in (the guitar, 32-bit signed, left-justified: full scale = +-2^31), stereo out, GP5_BLOCK
 * frames per call, GP5_SAMPLE_RATE Hz. The platform applies the output level (the volume knob) after
 * fw_process(). One block is 1.45 ms; the core runs at about 191.9 MHz: 278,000 cycles per block. The
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
#define GP5_SAMPLE_RATE 44100u
#define GP5_LARGE __attribute__((section(".large")))     /* put a big buffer in the 122 KB block */
#define GP5_FULL_SCALE 2147483648.0f                     /* in[] / GP5_FULL_SCALE = -1.0 .. 1.0 */

static void fw_init(void);
static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right);
static int  fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n);

static void     gp5_reply(const uint8_t *payload, uint32_t n);   /* one link frame; payload starts with [cmd][len] */
static void     gp5_ack(void);
static uint32_t gp5_get32(const uint8_t *p);

#ifndef GP5_HOST
#define GP5_CYCLES_LAST  (*(volatile uint32_t *)0x20007F74u)     /* core cycles of the last audio block */
#define GP5_CYCLES_WORST (*(volatile uint32_t *)0x20007F78u)     /* worst block since boot */
#define GP5_LATE_BLOCKS  (*(volatile uint32_t *)0x20007F7Cu)     /* blocks that finished after the next one was ready */
#else                                                             /* tools/run_effect.py, on the computer */
static uint32_t GP5_CYCLES_LAST, GP5_CYCLES_WORST, GP5_LATE_BLOCKS;
#undef GP5_LARGE
#define GP5_LARGE
#endif
#endif
