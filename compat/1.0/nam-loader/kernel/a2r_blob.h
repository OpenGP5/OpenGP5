/* A2-Lite capture file for the 32-bit-residual kernel (a2r.h): what a2r_export.py writes as .a2r and the pedal
 * keeps in its data area. Little-endian, 4-byte aligned:
 *   0  "A2R1"  4  length (bytes, whole file)  8  CRC-32 (zlib) of bytes 12 .. length-1
 *   12 nl u8, kh u8, X i8, eh i8, ehw i8, 3 zero   20 slope_q16 i32   24 rech[3] i32   36 hb f32   40 hs f32
 *   44 name, 24 bytes (UTF-8, zero-padded)
 *   68 per layer: K u8, 0, D u16, ez i8, er i8, 0, 0, b[3] i32, mix[3] i32, l1b[3] i32, l1w[9] i16, 0 0,
 *      then w[K*9] i16 ([o][i][k]) padded to 4 bytes
 *   then the head weights hw[3*kh] i16 ([c][k]) padded to 4 bytes
 * a2r_capture_parse() checks it and fills a model a2r_bind / a2r_bind_range take; the layers' conv weights are
 * read from the file itself during the bind (keep it where it is until then), the head weights are copied. */
#ifndef A2R_BLOB_H
#define A2R_BLOB_H
#include "a2r.h"

#define A2R_BLOB_MAGIC 0x31523241u     /* "A2R1" */

typedef struct {
    a2r_model_t m;
    a2r_layer_def_t layers[A2R_MAX_LAYERS];
    int16_t hw[A2R_C * A2R_MAX_KH];
    char name[25];
    uint32_t crc, length;
} a2r_capture_t;

uint32_t a2r_crc32(const uint8_t *p, unsigned n);
/* 0 = a good capture; < 0: -1 no / bad header, -2 bad checksum, -3 shape the kernel cannot run, -4 truncated */
int a2r_capture_parse(a2r_capture_t *c, const void *file, unsigned max_bytes);
/* Just the header's length and checksum fields, when the magic is there (0 otherwise): a cheap "did it change" */
uint32_t a2r_capture_crc_field(const void *file);
#endif
