#!/usr/bin/env python3
"""Extract SkyRoads assets to PNGs for verification.

Graphics LZS container (reverse engineered from SKYROADS.EXE):
  file := section+
  section := CMAP picture*
  CMAP := 'CMAP' u8 count, count*3 bytes VGA RGB (6-bit), count*2 bytes EGA pal
  picture := 'PICT' u16 screen_offset, u16 height, u16 width,
             LZS-compressed width*height 8bpp pixels
Pixel value 0 = transparent; nonzero pixel p displays as cmap[p] of the
section's palette (the game adds the section's global palette base to p and
loads the CMAP at that base — the two cancel out).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzs import Stream, decompress
from PIL import Image

OUT = 'extracted'


def vga_to_rgb(r, g, b):
    # 6-bit VGA DAC -> 8-bit, the standard (c<<2)|(c>>4) expansion
    return ((r << 2) | (r >> 4), (g << 2) | (g >> 4), (b << 2) | (b >> 4))


def walk_chunked(name, data):
    """Yield ('cmap', rgb_list) and ('pict', ofs, w, h, pixels)."""
    s = Stream(data, 0)
    while s.pos < len(data):
        tag = bytes([s.read_byte(), s.read_byte(), s.read_byte(), s.read_byte()])
        if tag == b'CMAP':
            count = s.read_byte()
            rgb = s.read_bytes(3 * count)
            s.read_bytes(2 * count)  # EGA palette, unused
            pal = [vga_to_rgb(rgb[i*3], rgb[i*3+1], rgb[i*3+2]) for i in range(count)]
            yield ('cmap', pal)
        elif tag == b'PICT':
            ofs = s.read_u16()
            h = s.read_u16()
            w = s.read_u16()
            pixels = decompress(s, w * h)
            yield ('pict', ofs, w, h, pixels)
        else:
            print(f"  {name}: unknown tag {tag!r} at {s.pos-5}, stopping")
            return


def export(name, path):
    data = open(path, 'rb').read()
    pal = [(0, 0, 0)] * 256
    n = 0
    base = os.path.splitext(os.path.basename(path))[0].lower()
    for chunk in walk_chunked(name, data):
        if chunk[0] == 'cmap':
            cpal = chunk[1]
            pal = cpal + [(255, 0, 255)] * (256 - len(cpal))
            print(f"  CMAP {len(cpal)} colors")
        else:
            _, ofs, w, h, pixels = chunk
            img = Image.new('RGB', (w, h))
            img.putdata([pal[p] for p in pixels])
            out = f'{OUT}/{base}_{n}_at{ofs % 320}x{ofs // 320}_{w}x{h}.png'
            img.save(out)
            print(f"  PICT #{n}: {w}x{h} @({ofs % 320},{ofs // 320}) -> {out}")
            n += 1


def export_roads():
    data = open('ROADS.LZS', 'rb').read()
    first = struct.unpack_from('<H', data, 0)[0]
    for i in range(first // 4):
        off, rawsize = struct.unpack_from('<HH', data, i * 4)
        s = Stream(data, off)
        meta = (s.read_u16(), s.read_u16(), s.read_u16())
        rgb = s.read_bytes(216)
        grid = decompress(s, rawsize)
        rows = rawsize // 14
        with open(f'{OUT}/road{i:02}.bin', 'wb') as f:
            f.write(grid)
        print(f"  road {i}: {rows} rows meta={meta}")


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for f in ['MAINMENU.LZS', 'CARS.LZS', 'DASHBRD.LZS', 'GOMENU.LZS',
              'SETMENU.LZS', 'HELPMENU.LZS', 'INTRO.LZS', 'ANIM.LZS',
              'WORLD0.LZS', 'WORLD1.LZS', 'WORLD2.LZS', 'WORLD3.LZS',
              'WORLD4.LZS', 'WORLD5.LZS', 'WORLD6.LZS', 'WORLD7.LZS',
              'WORLD8.LZS', 'WORLD9.LZS']:
        print(f"== {f}")
        try:
            export(f, f)
        except Exception as e:
            print(f"  FAILED: {e}")
    print("== ROADS.LZS")
    export_roads()
