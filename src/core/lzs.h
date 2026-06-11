/* Bluemoon LZS stream + decompressor (SKYROADS.EXE 0x62A0-0x6660).
 * See docs/FORMATS.md section 1. */
#ifndef SR_LZS_H
#define SR_LZS_H

#include "sr.h"

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;        /* index of byte AFTER the lookahead */
    uint8_t cur;       /* lookahead register */
    uint8_t bits;      /* bits remaining in cur (8 = fresh byte) */
} lzs_stream;

void     lzs_init(lzs_stream *s, const uint8_t *data, size_t size, size_t start);
uint8_t  lzs_byte(lzs_stream *s);
uint16_t lzs_u16(lzs_stream *s);
void     lzs_raw(lzs_stream *s, uint8_t *dst, size_t n);
uint32_t lzs_bits(lzs_stream *s, int n);
void     lzs_flush(lzs_stream *s);

/* Decompress exactly `size` bytes into dst (also flushes). */
void lzs_decompress(lzs_stream *s, uint8_t *dst, size_t size);

#endif
