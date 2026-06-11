#include "lzs.h"

void lzs_init(lzs_stream *s, const uint8_t *data, size_t size, size_t start)
{
    s->data = data;
    s->size = size;
    s->pos = start + 1;
    s->cur = start < size ? data[start] : 0;
    s->bits = 8;
}

static void advance(lzs_stream *s)
{
    s->cur = s->pos < s->size ? s->data[s->pos] : 0;
    s->pos++;
}

uint8_t lzs_byte(lzs_stream *s)
{
    uint8_t r = s->cur;
    advance(s);
    return r;
}

uint16_t lzs_u16(lzs_stream *s)
{
    uint16_t lo = lzs_byte(s);
    return (uint16_t)(lo | ((uint16_t)lzs_byte(s) << 8));
}

void lzs_raw(lzs_stream *s, uint8_t *dst, size_t n)
{
    while (n--)
        *dst++ = lzs_byte(s);
}

static int lzs_bit(lzs_stream *s)
{
    int bit = (s->cur >> 7) & 1;
    s->cur = (uint8_t)(s->cur << 1);
    if (--s->bits == 0) {
        s->bits = 8;
        s->cur = s->pos < s->size ? s->data[s->pos] : 0;
        s->pos++;
    }
    return bit;
}

uint32_t lzs_bits(lzs_stream *s, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--)
        v |= (uint32_t)lzs_bit(s) << i;
    return v;
}

void lzs_flush(lzs_stream *s)
{
    if (s->bits != 8)
        lzs_bits(s, s->bits);
}

void lzs_decompress(lzs_stream *s, uint8_t *dst, size_t size)
{
    int len_bits = lzs_byte(s);
    int off_bits = lzs_byte(s);
    uint32_t far_bias = 1u << off_bits;
    int far_bits = lzs_byte(s);
    size_t out = 0;

    while (out < size) {
        uint32_t dist;
        if (lzs_bit(s) == 0) {
            dist = lzs_bits(s, off_bits);
        } else if (lzs_bit(s) == 0) {
            dist = lzs_bits(s, far_bits) + far_bias;
        } else {
            dst[out++] = (uint8_t)lzs_bits(s, 8);
            continue;
        }
        uint32_t n = lzs_bits(s, len_bits);
        if (n + 1 >= size - out)
            break;                      /* terminator */
        size_t src = out - 2 - dist;    /* encoder never refs before start */
        for (uint32_t i = 0; i < n + 2; i++)
            dst[out + i] = dst[src + i];
        out += n + 2;
    }
    lzs_flush(s);
}
