/* A2-Lite capture files (a2r_blob.h). */
#include "a2r_blob.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t a2r_crc32(const uint8_t *p, unsigned n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

uint32_t a2r_capture_crc_field(const void *file)
{
    const uint8_t *f = (const uint8_t *)file;
    return rd32(f) == A2R_BLOB_MAGIC ? rd32(f + 8) : 0;
}

int a2r_capture_parse(a2r_capture_t *c, const void *file, unsigned max_bytes)
{
    const uint8_t *f = (const uint8_t *)file;
    uint32_t len, off;
    if (max_bytes < 68 || rd32(f) != A2R_BLOB_MAGIC) return -1;
    len = rd32(f + 4);
    if (len < 68 || len > max_bytes || (len & 3u)) return -1;
    if (a2r_crc32(f + 12, len - 12) != rd32(f + 8)) return -2;
    c->crc = rd32(f + 8); c->length = len;
    c->m.nl = f[12]; c->m.kh = f[13]; c->m.X = (int8_t)f[14]; c->m.eh = (int8_t)f[15]; c->m.ehw = (int8_t)f[16];
    c->m.slope_q16 = (int32_t)rd32(f + 20);
    for (int i = 0; i < A2R_C; i++) c->m.rech[i] = (int32_t)rd32(f + 24 + 4 * i);
    { union { uint32_t u; float f; } v; v.u = rd32(f + 36); c->m.hb = v.f; v.u = rd32(f + 40); c->m.hs = v.f; }
    for (int i = 0; i < 24; i++) c->name[i] = (char)f[44 + i];
    c->name[24] = 0;
    if (c->m.nl < 1 || c->m.nl > A2R_MAX_LAYERS || c->m.kh < 1 || c->m.kh > A2R_MAX_KH) return -3;
    off = 68;
    for (int l = 0; l < c->m.nl; l++) {
        a2r_layer_def_t *d = &c->layers[l];
        const uint8_t *p = f + off;
        if (off + 64 > len) return -4;
        d->K = p[0]; d->D = rd16(p + 2); d->ez = (int8_t)p[4]; d->er = (int8_t)p[5];
        for (int i = 0; i < A2R_C; i++) {
            d->b[i] = (int32_t)rd32(p + 8 + 4 * i); d->mix[i] = (int32_t)rd32(p + 20 + 4 * i); d->l1b[i] = (int32_t)rd32(p + 32 + 4 * i);
        }
        for (int i = 0; i < A2R_C * A2R_C; i++) d->l1w[i] = (int16_t)rd16(p + 44 + 2 * i);
        if (d->K < 1 || d->K > 15 || d->D < 1) return -3;
        off += 64;
        d->w = (const int16_t *)(f + off);
        off += ((uint32_t)d->K * A2R_C * A2R_C * 2u + 3u) & ~3u;
        if (off > len) return -4;
    }
    if (off + (((uint32_t)c->m.kh * A2R_C * 2u + 3u) & ~3u) > len) return -4;
    for (int i = 0; i < A2R_C * c->m.kh; i++) c->hw[i] = (int16_t)rd16(f + off + 2 * i);
    c->m.hw = c->hw;
    c->m.layers = c->layers;
    return 0;
}
