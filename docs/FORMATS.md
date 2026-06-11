# SkyRoads data formats — reverse engineered specification

All multi-byte integers are little-endian. "VGA RGB" = 3 bytes per color,
6 bits per component (0..63).

Source of truth: SKYROADS.EXE disassembly (see re/MAP.md for function
addresses). Every format below has been validated by decoding the retail
files successfully (tools/lzs.py, tools/extract.py).

## 1. LZS bitstream & compression

### 1.1 Stream model
The engine reads files through a 4KB buffered stream with a one-byte
lookahead register:

- `read_byte()` returns the lookahead and refills it with the next file byte.
- `read_bit()` consumes the lookahead MSB-first; after the 8th bit the next
  file byte is loaded. Byte- and bit-reads share the same lookahead.
- `read_bits(n)`: n single bits; the first bit read lands in the highest
  result position (big-endian bit order). n=0 returns 0.
- `read_u16()`: two byte-reads, little-endian.
- `flush()`: if a byte is partially consumed, discard its remaining bits
  (called once at the end of every compressed block).

### 1.2 Compressed block (EXE 0x6660)
```
u8 len_bits, u8 off_bits, u8 far_bits     ; 3 raw header bytes
repeat until output_size bytes produced:
  bit == 0:        dist = bits(off_bits)                 ; near match
  bits == 1,0:     dist = bits(far_bits) + (1<<off_bits) ; far match
  bits == 1,1:     literal: output(bits(8))
  for matches:
    n = bits(len_bits)
    if n+1 >= bytes_remaining: STOP (terminator)
    copy n+2 bytes from output_pos - 2 - dist (overlap allowed)
flush()
```
The decompressed size is always known from context (it is never stored in
the block itself).

## 2. Graphics container (*.LZS with CMAP/PICT chunks)

```
file     := section+
section  := CMAP picture*
CMAP     := "CMAP" u8 count, count*3 VGA RGB, count*2 EGA palette bytes
picture  := "PICT" u16 dummy, u16 screen_offset, u16 height, u16 width,
            compressed-block of width*height 8bpp pixels
```
- screen_offset = y*320+x destination in mode 13h.
- Pixel 0 is transparent. The engine adds a per-file palette base to every
  nonzero pixel and loads the CMAP at the same base, so pixel p renders as
  cmap[p]. CMAP entry 0 is always black (never referenced by pixels).
- Palette bases (from loader code): roads/world palette buffer ds:0x41d0:
  road=0(72 colors), cars=72(20), dashboard=92(50), world=142(114).
  Menu palette buffer ds:0x5182: intro=0, gomenu=0 + second section 240,
  mainmenu=190(3), setmenu=200(34) + second section 250, helpmenu=200.
- Files: MAINMENU(3 pics 68x57@127,128), SETMENU(fullscreen+10 overlays),
  HELPMENU(3 fullscreens), GOMENU(fullscreen + 6x5 cursor), INTRO,
  CARS(one 24x2310 strip), DASHBRD(VGA pic + EGA pic, 320x71@0,129),
  WORLD0..9(320x138 backdrop). ANIM.LZS uses an 'ANIM' container (TBD).

### CARS.LZS strip layout
24px wide, 2310 tall = sequence of 24x30 sprites (720 bytes each):
explosion frames then ship frames; play_road computes sprite address as
`((a*3 + b)*3 + 14) * 720` — exact indexing TBD (ship tilt/jump variants).

## 3. ROADS.LZS (level data)

```
directory: N entries of { u16 offset, u16 raw_size }, N = first offset / 4
           (retail: N=31; entry 0 = demo road, 1..30 = game roads)
entry at offset:
  u16 gravity        ; dash gauge shows (gravity-3)*100; typical 8, range 4..20
  u16 fuel?          ; word2, range 50..225 (semantics TBD from physics code)
  u16 oxygen?        ; word3, range 2..180
  byte[216]          ; 72-color road palette -> global palette base 0
  compressed-block   ; raw_size bytes = rows of 7 u16, raw_size/14 rows
```
Grid: 7 columns x rows u16 tile codes, row 0 = start (ship spawns row 3).
Game buffer is 500 rows max, zero-filled (ds:0x1638). Tile word:

```
bits 0-3   ground surface type/color (0 = void; whole word 0 = hole)
bits 4-7   block-top surface type/color
bits 8-11  block geometry: 0 none, 1 tunnel, 2 half block, 3 half block
           with tunnel bore, 4 full block, 5 full block with bore
bits 12-15 unused (0 in all 31 retail roads)
```

Surface values: 0x2 sticky (-0x12f speed/tick, lateral +0x618 bonus
removed), 0x8 slippery ice (xvel frozen), 0x9 supplies (fuel/oxy refill),
0xA boost (+0x12f speed/tick), 0xC burning (death); other values are plain
floor drawn in palette quad `nibble*3`. Full details with EXE addresses in
`re/notes/gameloop.md` §8; `tools/make_track.py` builds a playable custom
road from these rules.

## 4. TREKDAT.LZS (engine graphics objects)

Sequence until EOF: { u16 raw_size, u16 lzs_size, compressed-block }.
Loaded into per-object buffers of raw_size bytes; LZS output written at
offset raw_size-lzs_size; u16 at offset 0 set to that offset (second-stage
in-place expansion deferred; object meanings TBD - tile textures, fonts,
sphere sprites...). Object segment table ds:0xe82, count ds:0x54b4.

## 5. FUL_DISP.DAT / OXY_DISP.DAT (dashboard gauge shapes)

10 records; u16 offsets[10] relative to end of offset table (file pos 20),
record: { u16 screen_offset, u8 w, u8 h, w*h bytes of 0/1/2 }
= incremental lit-segment shapes for the fuel/oxygen bar gauges
(e.g. first record at screen (146,170)). Values select segment color.

## 6. SPEED.DAT

3903 bytes, parsed by callback (34 entries, ds:0x4580) - speedometer
digit/needle shapes (format TBD).

## 7. DEMO.REC

6398 (0x18FE) bytes read raw into ds:0x962e; demo input recording played
on the demo road (entry 0). Format TBD (paired with road scroll - input
state per tick).

## 8. Sound

- SFX.SND / INTRO.SND: raw 8-bit digitized samples for Sound Blaster
  (DSP detected by port scan 0x210..0x260, DMA programmed via 0x5b1d,
  play via DSP cmd 0x40 time-constant + 0x14 single-cycle DMA).
  PC-speaker fallback: frequency words streamed by timer ISR from [0xba0].
- MUZAX.LZS: AdLib OPL2 music. Directory: 15 songs x 8 bytes
  { u16 ?, u16 ?, u16 offset?, u16 size? } (exact layout TBD).
  Song 0 = menu, gameplay random 2..13. OPL2 driver at 0x5876/0x58b1
  (instrument tables ds:0xc90, 11 bytes/patch; note tables ds:0xc77/0xc83;
  channels 7-8 = engine hum, frequency follows speed).

## 9. Timing & input (for exact game feel)

- PIT channel 0 reprogrammed to divisor 0x19E4 (6628) = 180.02 Hz.
- ISR phases 0..9 cyclically: music tick every IRQ (180 Hz); game frame
  counter [0x160c] incremented on 2 of 10 phases = exactly 36.0 fps;
  original BIOS int8 chained 1 of 10 = 18.0 Hz (DOS clock kept correct).
- Keyboard: custom int9 ISR; scancode state array ds:0xba2:
  up,down,left,right,home,pgup,end,pgdn,ESC,SPACE('jump'),P('pause');
  bit7 = currently held. Steering = (right|pgup|pgdn) - (left|home|end)
  evaluated each frame; accelerate/brake from up/down groups similarly.
- skyroads.cfg: settings + per-road completion flags (30 u16 at ds:0x452a,
  written after each completed road; format TBD ~ 0x5721/0x5770).

## 10. Screen layout

- Mode 13h 320x200x256. Rows 0..137: 3D viewport (back buffer 0xAC80
  bytes in RAM for VGA). Rows 138..199: static dashboard, restored from
  pristine copy each road; dashboard PICT covers 129..199 (top 9 rows
  drawn over the viewport edge).
- Global game palette: [0..71] road, [72..91] car, [92..141] dashboard,
  [142..255] world backdrop. Menu palette separate.
- Palette fades: 0x4b72(palette, step, 36) - 36-step fade via VGA DAC.
