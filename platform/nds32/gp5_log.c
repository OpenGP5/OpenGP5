/* gp5_log: lines of text from the firmware, the platform and the DSP, kept in a 4 KB ring on the control processor
 * and read by a computer (USB 7E 18; gp5_link.py log, Launchpad's console). Each line starts with the time since
 * start, "[  12.345] ", and lines from the DSP with "dsp: ". When nobody reads, old lines are overwritten.
 *
 * Safe from the main loop and from the audio link's interrupt (a line is put in with interrupts off, which takes a
 * few microseconds; the formatting before it, with interrupts on, costs more: keep logging out of every block). */
#include <stdarg.h>
#include <stdint.h>
#include <nds32_intrinsic.h>
#include "gp5_firmware.h"
#include "gp5_fmt.h"

#define LOG_SIZE 4096u                               /* a power of two */
#define LINE_MAX 128u
/* in the low RAM (app.ld .lowram: 0x20000000..0x20003000, the vendor SDK's flash-driver area, unused by this platform;
 * not cleared at start-up): platform buffers there leave the firmware's RAM alone */
#if defined(__nds32__)
#define LOWRAM __attribute__((section(".lowram")))
#else
#define LOWRAM                                 /* the computer (tests, simulator): ordinary memory */
#endif
static LOWRAM char ring[LOG_SIZE];
static volatile uint32_t log_w;                      /* bytes written since start (the ring holds the last LOG_SIZE) */

/* one finished line into the ring (with its newline) */
static void put_line(const char *s, unsigned n)
{
    uint32_t psw = __nds32__mfsr(NDS32_SR_PSW), w;
    unsigned i;
    __nds32__setgie_dis();
    w = log_w;
    for (i = 0; i < n; i++) ring[(w + i) & (LOG_SIZE - 1u)] = s[i];
    ring[(w + n) & (LOG_SIZE - 1u)] = '\n';
    log_w = w + n + 1u;
    if (psw & 1u) __nds32__setgie_en();
}

static unsigned stamp(char *b)
{
    uint32_t ms = gp5_ms();
    return gp5_fmt(b, 16, "[%5u.%03u] ", ms / 1000u, ms % 1000u);
}

void gp5_log(const char *fmt, ...)
{
    char line[LINE_MAX];
    va_list ap;
    unsigned n = stamp(line);
    va_start(ap, fmt);
    n += gp5_vfmt(line + n, LINE_MAX - n, fmt, ap);
    va_end(ap);
    while (n && line[n - 1] == '\n') n--;            /* (a newline of its own is not needed) */
    put_line(line, n);
}

/* a line the DSP sent (dsp_link.c: frame 0x4F10 [text]) */
void gp5_log_dsp(const uint8_t *text, unsigned len)
{
    char line[LINE_MAX];
    unsigned n = stamp(line), i;
    const char *pre = "dsp: ";
    for (i = 0; pre[i]; i++) line[n++] = pre[i];
    for (i = 0; i < len && text[i] && n < LINE_MAX - 1u; i++) line[n++] = (text[i] >= 32 && text[i] < 127) ? (char)text[i] : '?';
    put_line(line, n);
}

/* USB 7E 18 <from u32>: up to `max` bytes of the log starting at byte `from` (or the oldest still kept, if that one
 * was overwritten). Returns how many; *at = where they start, *end = bytes written so far. */
unsigned gp5_log_read(uint32_t from, uint8_t *out, unsigned max, uint32_t *at, uint32_t *end)
{
    uint32_t w = log_w, oldest = w > LOG_SIZE ? w - LOG_SIZE : 0u;
    unsigned n = 0;
    if (from < oldest || from > w) from = from > w ? w : oldest;
    while (n < max && from + n < w) { out[n] = (uint8_t)ring[(from + n) & (LOG_SIZE - 1u)]; n++; }
    *at = from; *end = w;
    return n;
}
