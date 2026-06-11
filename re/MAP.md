# SKYROADS.EXE function & data map

Borland C/C++ (286 codegen), small model. Image: code 0x0000-0x66DF, data seg
at paragraph 0x66E (DS-relative addresses below). Entry 0x60D0 (c0 startup),
`main` = **0x1b8**. BSS 0xE28..0xB150 zeroed at startup; stack top ss:0xB150.

## Top-level flow (main 0x1b8)
- 0x5ffd detect VGA -> [0x36]=1, else 0x5fe9 detect EGA or die ("requires EGA/VGA")
- 0x5a7c, 0x58b1, 0x3ab0: init subsystems
- 0x0000 set_video_mode (13h VGA / 0Dh EGA via [0x36]), saves ROM 8x8 font ptrs
  ([0x3194],[0x3198]) via int 10h ax=1130h bh=3/4
- 0x571b load skyroads.cfg ("skyroads.cfg" @ds:0xbcc)
- 0x57a8(n) music_start(song n) (0=menu)
- 0x54ec load oxy_disp.dat ful_disp.dat speed.dat demo.rec (-> ds:0x9604,0x961a,
  0x4580 tables; demo buf ds:0x962e, 0x18fe bytes)
- 0x00bb load trekdat.lzs -> object segment table ds:0xe82, count [0x54b4]
  record: u16 rawsize, u16 lzs_size, bitstream; LZS output placed at
  buffer[rawsize-lzs_size..rawsize], buffer[0]=u16 (rawsize-lzs_size)
  (second-stage in-place expansion happens lazily elsewhere)
- 0x2cb2 intro sequence
- 0x4e36(first_time) main menu; selection -> [0x9602] (3 = demo)
- loop:
  - 0x554b load cars.lzs (ship+explosion strip; pal base 72)
  - demo: road=0x55f8(0), world=0x536b(0)
  - else 0x5164 gomenu select -> [0x933e] road 0..29;
    music 0x57a8(2 + (0x19c()%12)); 0x55f8(road+1); 0x536b(road/3)
  - 0x707 PLAY ROAD (gameplay loop)
  - postgame: completion flags array ds:0x452a (30 words)

## File I/O layer
- 0x5c38 open(name) -> handle [0x41b8]; 0x5c1a close; 0x5f3f read(h,buf,n);
  0x5c67 lseek(h, off32, whence); 0x5bf5 io_ok_check; 0x5c0a fail(code)
- 0x639a stream_install(handle, ?, bufsize=0x1000, flag) - buffered reader,
  buffer ds:0x31b4..0x41b4, lookahead byte [0x41bc], bitcount [0x41ba],
  ptr [0x41c2], end [0x41c0]
- 0x640a read_byte (returns lookahead, advances)
- 0x64f0 read_u16le
- 0x6425 read_bit (MSB-first via rcl on lookahead)
- 0x6479 read_bits(n) (first bit = MSB)
- 0x650f read_raw(farptr, n)
- 0x65f3 flush_to_byte_boundary
- 0x6660 LZS decompress(off, seg, size) - SELF-MODIFYING: patches bit widths
  hdr: len_bits, off_bits, far_bits. codes: 0=near match, 10=far match
  (dist += 1<<off_bits), 11=literal(8 bits). dist from out_pos-2.
  len = read(len_bits)+2; len+1 >= remaining -> terminate. flush at end.

## Asset loaders
- 0x40ff lzs_open(name): open + stream_install(4KB)
- 0x4120 lzs_close
- 0x3f75 read_cmap_into(buf, base): skip 4-byte tag, u8 count, 3n RGB(6-bit)
  -> buf+3*base, then 2n EGA bytes -> scratch 0xaf42/0xaf4c
- 0x3fc6 read_cmap_alloc(obj, base)
- 0x4068 read_pict(obj, colorbase): skip u16; raw 8: u16 dummy(->ptr),
  u16 screen_ofs, u16 height, u16 width; alloc w*h; decompress;
  VGA: 0x4036 add colorbase to nonzero pixels; EGA: 0x3d82 convert
  obj struct: [0]=pixbuf seg, [2]=screen_ofs, [4]=h, [6]=w
- 0x3ed8 seg_alloc(u32 size) -> segment (tracked in handle table [0x4572],
  table at ds:-0x6cbc?? bx-relative = 0x9344?), 0x3f04 push_mark,
  0x3f1f free_to_mark, 0x3f56 free_one
- 0x55f8 load_road(n): roads.lzs, dir entry n at n*4: u16 off, u16 rawsize;
  at off: u16 gravity?, u16 fuel?, u16 oxy?; 216-byte palette -> global pal
  base 0 (72 colors); decompress rawsize -> ds:0x1638 grid (7 u16/row),
  cleared to 0x1b58 bytes (500 rows max). returns rows = rawsize/14.
  [0x456e],[0x54ae],[0x4574] = the 3 header words.
- 0x536b load_world(n): "world%d.lzs", cmap -> global pal base 0x8E (142),
  PICT 320x138 backdrop
- 0x554b load_cars: cmap base 0x48 (72); PICT 24x2310 strip -> far ptr
  [0xaf44/0xaf46]
- 0x5580 load_dashbrd: cmap base 0x5C (92); VGA: PICT#1; EGA: PICT#2 ->
  obj ds:0x54a4
- mainmenu: cmap base 0xBE (190) menu pal ds:0x5182; 3 PICTs 68x57@(127,128)
- setmenu: base 0xC8 (200) + second section base 0xFA (250); 1 full screen +
  10 overlays (5 items x2 states)
- helpmenu (0x4dac x2): base 0xC8; full screens
- gomenu (0x5164): base 0 + section base 0xF0 (240); fullscreen + 6x5 sprite
- intro: base 0 etc; ANIM.LZS: 'ANIM' tag container (TODO)
- music 0x57a8(n): muzax.lzs (loader near 0x57c7)

## Memory map (DS-relative)
- 0x36: VGA flag (1=VGA mode 13h, 0=EGA 0Dh)
- 0x1638: road grid buffer (500 rows x 7 u16)
- 0x31b4-0x41b4: 4KB file buffer
- 0x41d0: global game palette (256x3 bytes, 6-bit VGA)
- 0x5182: menu palette (256x3)
- 0x4526: cfg: last menu selection?; 0x452a: 30x u16 road-completed
- 0x933e: selected road 0..29
- 0x9602: menu action (3=demo)
- 0x9604/0x961a: oxy/ful gauge tables; 0x962e demo.rec buffer
- 0xe82: trekdat object segment table; 0x54b4 count
- 0xaf44: cars strip far ptr
- 0x456e/0x54ae/0x4574: road header words (gravity/fuel/oxy TBD)

## Gameplay (TODO)
- 0x707: play_road main gameplay loop
- 0x2cb2: intro
- 0x19c: PIT-based rand
- int 33h mouse @0x694+; keyboard/timer hooks via 0x5a7c?

## Additional findings (session 2)
- 0x2cb2: renderer init — installs draw dispatch ptrs [0xe44..0xe4a]:
  VGA 0x348b/0x3137/0x3174/0x323f, EGA 0x3462/0x3083/0x30d9/0x31bf
- fn_4575: intro sequence. intro.lzs read order: palobj@0, PICT@0 (title),
  palobj@0x32 x2, PICT@0x32, then 7x { palobj@0xa0 x2, PICT@0xa0 } (logo
  frames with fade palette pairs). Then anim.lzs: 'ANIM' tag + u16 count,
  CMAP-obj@0, frame data (intro animation), INTRO.SND digitized voice.
- fn_4e36(first): main menu. Loads mainmenu.lzs 3 box PICTs @190 into
  stack objs; intro.lzs first CMAP + title PICT @0; VGA: instant DAC write
  of 3 colors at 190 from ds:0x53bc; crossfade machinery via palette-objs
  (0x4260 apply, 0x4315 lerp two palettes over 0x32=50 steps).
- fn_5164: gomenu (road select). music_start(1). Completion ticks: base ofs
  0x11F0=(112,14), road pitch 9 rows, world pitch 39 rows, +0xA0 right col,
  7px per tick, max 7. Navigation: BIOS getkey; up/down ±1 clamp 0..29,
  left/right ∓15; ENTER→0, ESC→1. Selection highlight fn_5064: save-under
  48x9 at ofs 0xF3E=(62,12) + same pitches; threshold blit (colors <0x63
  = background restored).
- music_start 0x57a8(n): muzax.lzs dir = 6 bytes/song {u16 off, u16
  n_instr, u16 raw_size}; song decompressed to ds:0x54bc (max 0x3e80);
  instruments n_instr*16 bytes at head; player start 0x5a61(base,
  base+n_instr*16); current song [0xbc2]; sound-off flag [0x4528].
- SB detect 0x5a7c region: port scan 0x210..0x260, [0xcc2]=base port,
  [0xcc4]=DMA ch; 0x5b1d DMA program; 0x5b5a play(off,seg,len) DSP 0x40+
  time constant, then 0x14 block. fn_03c2 = play_sfx(n).
- fn_5c92(dst_off,dst_seg,ofs,mode): screen block read/write (save-under).
- Mouse: int 33h wrappers near 0x694 (reset/show/hide/status).
