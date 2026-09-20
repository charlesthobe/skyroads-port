#!/usr/bin/env python3
"""Build a custom SkyRoads track and splice it into a copy of ROADS.LZS.

Demonstrates that the reverse-engineered formats are complete enough to
author new content. Usage:

    python3 tools/make_track.py /path/to/gamedir /path/to/outdir [entry]

Copies the game dir's ROADS.LZS, replaces road <entry> (default 1, the
first selectable road) with the demo track below, and writes outdir/
ROADS.LZS. Point the engine at outdir (with the other data files copied
or symlinked beside it) to play it.

Format (see docs/FORMATS.md and re/notes/gameloop.md §8):
  directory: N x { u16 offset, u16 raw_size }, N = first_offset/4
  entry:     u16 gravity, u16 fuel, u16 oxygen,
             byte[216] road palette (72 x VGA RGB, 0..63),
             LZS-compressed block of rows*14 bytes (7 u16 tiles per row)
  tile word: bits0-3 ground surface, bits4-7 block-top surface,
             bits8-11 geometry (0 none, 1 tunnel, 2 half block,
             3 half+bore, 4 full block, 5 full+bore)
  surfaces:  0 void, 2 sticky, 8 ice, 9 supplies, 0xA boost, 0xC burning,
             anything else = plain floor in palette color (nibble*3 quad)
"""
import struct
import sys

# ---------------------------------------------------------------- LZS ----
def lzs_encode_literals(data: bytes) -> bytes:
    """Valid Bluemoon-LZS stream using only literal codes (no matching).
    Header picks 8-bit fields; each byte costs 10 bits. Decoder stops when
    the expected output size is reached, so no explicit terminator needed,
    but we emit a zero match as a guard then flush to a byte boundary."""
    bits = []
    def put(v, n):
        for i in range(n - 1, -1, -1):
            bits.append((v >> i) & 1)
    out = bytearray([8, 8, 8])           # len_bits, off_bits, far_bits
    for b in data:
        put(0b11, 2)                     # literal code
        put(b, 8)
    put(0, 1); put(0, 8); put(0, 8)      # guard match (len 0+2 >= remaining)
    while len(bits) % 8:
        bits.append(0)
    for i in range(0, len(bits), 8):
        out.append(int("".join(map(str, bits[i:i+8])), 2))
    return bytes(out)

# ------------------------------------------------------------- palette ----
def neon_palette():
    """72 VGA colors (0..63 per channel), 24 quads of 3 (light/mid/dark)."""
    pal = []
    for q in range(24):
        base = [(20 + q, 0, 40), (50, 10 + q, 0), (0, 40, 30 + q)][q % 3]
        for shade, k in ((1.0, 0), (0.66, 1), (0.4, 2)):
            pal += [min(63, int(c * shade)) for c in base]
    return bytes(pal)

# --------------------------------------------------------------- track ----
PLAIN, STICKY, ICE, SUPPLY, BOOST, BURN = 0x3, 0x2, 0x8, 0x9, 0xA, 0xC
def tile(ground=0, top=0, geom=0):
    return ground | (top << 4) | (geom << 8)

F  = tile(PLAIN)               # plain floor
T  = tile(PLAIN, PLAIN, 1)     # tunnel
H  = tile(PLAIN, PLAIN, 2)     # half block on floor
FB = tile(PLAIN, PLAIN, 4)     # full block on floor
B  = tile(BOOST)
S  = tile(SUPPLY)
_  = 0                         # hole

def fable_road():
    rows = []
    rows += [[F] * 7] * 6                                  # runway
    for i in range(8):                                     # half-block slalom
        r = [F] * 7
        r[1 if i % 2 else 5] = H
        rows.append(r)
    rows += [[_, _, F, B, F, _, _]] * 6                    # narrow boost bridge
    rows += [[F, T, F, F, F, T, F]] * 5                    # tunnel stretch
    rows += [[F, F, F, S, F, F, F]]                        # supplies
    rows += [[FB, F, F, F, F, F, FB]] * 6                  # full-block canyon
    rows += [[_, _, _, F, _, _, _]] * 8                    # tightrope
    rows += [[F] * 7] * 6
    rows += [[T] * 7] * 7                                  # finish gate
    return rows

def build_road(rows, gravity=8, fuel=150, oxygen=60, palette=None):
    grid = b"".join(struct.pack("<7H", *r) for r in rows)
    return (struct.pack("<3H", gravity, fuel, oxygen)
            + (palette or neon_palette())
            + lzs_encode_literals(grid)), len(grid)

def splice(roads: bytes, entry: int, road: bytes, raw_size: int) -> bytes:
    n = struct.unpack_from("<H", roads, 0)[0] // 4
    dirs = [struct.unpack_from("<2H", roads, i * 4) for i in range(n)]
    blobs = []
    for i, (off, sz) in enumerate(dirs):
        end = dirs[i + 1][0] if i + 1 < n else len(roads)
        blobs.append(roads[off:end])
    blobs[entry] = road
    sizes = [s for _, s in dirs]
    sizes[entry] = raw_size
    out, pos = bytearray(), n * 4
    for blob, sz in zip(blobs, sizes):
        out += struct.pack("<2H", pos, sz)
        pos += len(blob)
    return bytes(out) + b"".join(blobs)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    entry = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    roads = open(f"{src}/ROADS.LZS", "rb").read()
    road, raw = build_road(fable_road())
    out = splice(roads, entry, road, raw)
    import os
    os.makedirs(dst, exist_ok=True)
    open(f"{dst}/ROADS.LZS", "wb").write(out)
    print(f"wrote {dst}/ROADS.LZS ({len(out)} bytes), "
          f"road {entry} replaced: {raw//14} rows")
