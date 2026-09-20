#!/usr/bin/env python3
"""Bluemoon LZS stream + decompressor, reverse engineered from SKYROADS.EXE.

Stream model (from EXE 0x62A0-0x65F2 buffered-IO layer):
  - A one-byte lookahead register (`cur`) is primed at open/seek.
  - read_byte returns `cur` and advances.
  - read_bit consumes `cur` MSB-first; after 8 bits the next file byte is
    loaded. read_bits(n) returns n bits, first bit read = most significant.
  - flush() discards the rest of a partially consumed byte (EXE 0x65F3).

Decompressor (EXE 0x6660, self-modifying code patching 3 bit-width params):
  header: len_bits, off_bits, far_bits (3 raw bytes)
  then per item:
    bit 0            -> near match: dist = bits(off_bits)
    bit 1 + bit 0    -> far match:  dist = bits(far_bits) + (1 << off_bits)
    bit 1 + bit 1    -> literal:    byte = bits(8)
  match: n = bits(len_bits); if n+1 >= bytes_remaining: END
         else copy n+2 bytes from out_pos - 2 - dist
"""
from __future__ import annotations


class Stream:
    def __init__(self, data: bytes, pos: int = 0):
        self.data = data
        self.pos = pos + 1
        self.cur = data[pos]  # primed lookahead
        self.bits = 8

    def _advance(self):
        if self.pos < len(self.data):
            self.cur = self.data[self.pos]
        else:
            self.cur = 0
        self.pos += 1

    def read_byte(self) -> int:
        r = self.cur
        self._advance()
        return r

    def read_u16(self) -> int:
        lo = self.read_byte()
        return lo | (self.read_byte() << 8)

    def read_bytes(self, n: int) -> bytes:
        return bytes(self.read_byte() for _ in range(n))

    def read_bit(self) -> int:
        bit = (self.cur >> 7) & 1
        self.cur = (self.cur << 1) & 0xFF
        self.bits -= 1
        if self.bits == 0:
            self.bits = 8
            if self.pos < len(self.data):
                self.cur = self.data[self.pos]
            else:
                self.cur = 0
            self.pos += 1
        return bit

    def read_bits(self, n: int) -> int:
        v = 0
        for i in range(n - 1, -1, -1):
            v |= self.read_bit() << i
        return v

    def flush(self):
        if self.bits != 8:
            self.read_bits(self.bits)


def decompress(s: Stream, size: int) -> bytes:
    out = bytearray()
    len_bits = s.read_byte()
    off_bits = s.read_byte()
    far_bias = 1 << off_bits
    far_bits = s.read_byte()
    while len(out) < size:
        if s.read_bit() == 0:
            dist = s.read_bits(off_bits)
        elif s.read_bit() == 0:
            dist = s.read_bits(far_bits) + far_bias
        else:
            out.append(s.read_bits(8))
            continue
        n = s.read_bits(len_bits)
        if n + 1 >= size - len(out):
            break  # terminator: match length >= remaining
        src = len(out) - 2 - dist
        for i in range(n + 2):
            out.append(out[src + i])
    s.flush()
    return bytes(out)


if __name__ == '__main__':
    import struct
    import sys
    data = open(sys.argv[1] if len(sys.argv) > 1 else 'ROADS.LZS', 'rb').read()
    # ROADS.LZS: directory of (u16 offset, u16 raw_size) entries; the first
    # entry's offset = directory size, so entry count = offset/4.
    first_off = struct.unpack_from('<H', data, 0)[0]
    n = first_off // 4
    print(f"directory: {n} entries")
    for i in range(n):
        off, rawsize = struct.unpack_from('<HH', data, i * 4)
        if off == 0 and rawsize == 0:
            print(f"  [{i:2}] empty")
            continue
        s = Stream(data, off)
        meta = (s.read_u16(), s.read_u16(), s.read_u16())
        pal = s.read_bytes(216)  # 72 colors x 3 (VGA 6-bit RGB)
        out = decompress(s, rawsize)
        rows = rawsize // 14
        ok = 'OK' if len(out) == rawsize else f'SHORT {len(out)}'
        print(f"  [{i:2}] off={off:#06x} size={rawsize:5} rows={rows:3} meta={meta} -> {ok}")
