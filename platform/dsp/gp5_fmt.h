/* A small printf for both processors (neither has a C library): gp5_log uses it, a firmware may too.
 *   %d %i %u %x %X %c %s %%, %f with an optional precision (%.2f; default 6; floats only as far as +-2e9),
 *   width, zero padding and left alignment (%5d, %08x, %-8s), the l modifier (%lu, ignored: int and long are both 32 bits here).
 * Writes at most max - 1 characters and a NUL; returns the length written. Header-only. */
#ifndef GP5_FMT_H
#define GP5_FMT_H
#include <stdarg.h>
#include <stdint.h>

static unsigned gp5__put_u(char *o, unsigned n, unsigned max, uint32_t v, unsigned base, int upper, unsigned width, char pad, int neg)
{
    char t[12];
    unsigned k = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { t[k++] = dig[v % base]; v /= base; } while (v);
    if (neg && pad == '0' && n + 1 < max) { o[n++] = '-'; neg = 0; if (width) width--; }
    if (neg) t[k++] = '-';
    while (k < width && n + 1 < max) { o[n++] = pad; width--; }
    while (k && n + 1 < max) o[n++] = t[--k];
    return n;
}

static __attribute__((unused)) unsigned gp5_vfmt(char *o, unsigned max, const char *f, va_list ap)
{
    unsigned n = 0;
    if (!max) return 0;
    for (; *f && n + 1 < max; f++) {
        unsigned width = 0, prec = 6, start;
        char pad = ' ';
        int left = 0;
        if (*f != '%') { o[n++] = *f; continue; }
        f++;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { pad = left ? ' ' : '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10u + (unsigned)(*f++ - '0');
        if (*f == '.') { f++; prec = 0; while (*f >= '0' && *f <= '9') prec = prec * 10u + (unsigned)(*f++ - '0'); if (prec > 6) prec = 6; }
        while (*f == 'l') f++;
        start = n;
        unsigned lw = left ? width : 0u;              /* left-aligned: fill after the value instead */
        if (left) width = 0;
        switch (*f) {
        case 'd': case 'i': { int32_t v = va_arg(ap, int32_t); n = gp5__put_u(o, n, max, v < 0 ? 0u - (uint32_t)v : (uint32_t)v, 10, 0, width, pad, v < 0); break; }
        case 'u': n = gp5__put_u(o, n, max, va_arg(ap, uint32_t), 10, 0, width, pad, 0); break;
        case 'x': n = gp5__put_u(o, n, max, va_arg(ap, uint32_t), 16, 0, width, pad, 0); break;
        case 'X': n = gp5__put_u(o, n, max, va_arg(ap, uint32_t), 16, 1, width, pad, 0); break;
        case 'c': o[n++] = (char)va_arg(ap, int); break;
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; while (*s && n + 1 < max) o[n++] = *s++; break; }
        case 'f': {
            double d = va_arg(ap, double);
            uint32_t scale = 1, ip, fp;
            unsigned k;
            int neg = d < 0;
            if (neg) d = -d;
            for (k = 0; k < prec; k++) scale *= 10u;
            if (d > 2e9) d = 2e9;
            ip = (uint32_t)d; fp = (uint32_t)((d - (double)ip) * scale + 0.5);
            if (fp >= scale) { ip++; fp -= scale; }
            n = gp5__put_u(o, n, max, ip, 10, 0, width > prec + 1 ? width - prec - 1 : 0, pad, neg);
            if (prec && n + 1 < max) { o[n++] = '.'; n = gp5__put_u(o, n, max, fp, 10, 0, prec, '0', 0); }
            break;
        }
        case '%': o[n++] = '%'; break;
        case 0: f--; break;
        default: o[n++] = '%'; if (n + 1 < max) o[n++] = *f; break;
        }
        while (n - start < lw && n + 1 < max) o[n++] = ' ';
    }
    o[n] = 0;
    return n;
}

static __attribute__((unused)) unsigned gp5_fmt(char *o, unsigned max, const char *f, ...)
{
    va_list ap;
    unsigned n;
    va_start(ap, f); n = gp5_vfmt(o, max, f, ap); va_end(ap);
    return n;
}
#endif
