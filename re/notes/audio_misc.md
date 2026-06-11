# SkyRoads — audio, config, intro/ANIM, text & fade subsystems

Evidence convention: `ADDR: instruction` quotes re/full.asm (ndisasm of re/image.bin;
`shl x,0x0` = shift by 1). Data-segment bytes quoted from re/image.bin at file
offset 0x66E0+addr (initialized data is ds:0x0000..0x0E27; addresses >= 0xE28 are BSS,
zeroed at startup). Borland C: args pushed right-to-left, [bp+4] = first arg.

--------------------------------------------------------------------------------
## 0. Global sound state

| var | meaning |
|---|---|
| ds:0xcc2 | Sound Blaster base port (0x210..0x260), 0 = absent. Image init = `10 02` (0x210 = port-scan start). |
| ds:0xcc4 | SB DMA channel, init = 1 (never changed; `01 00` in image). |
| ds:0xcc6 | 4-byte DMA page-register table {0x87,0x83,0x81,0x82} for ch 0-3 (`5B50: mov dl,[bx+0xcc6]`). |
| ds:0x4528 | sound-off flag from skyroads.cfg (0=on). Gates BOTH music (`57BD: cmp word [0x4528],0x0` in music_start) and sfx (`3CE: cmp word [0x4528],0x0` in play_sfx). |
| ds:0xbc2 | currently playing song #, init 0xFFFF (`ff ff` in image). |
| ds:0xba0 | PC-speaker stream pointer (see §2/§4). Init value 0x319C — points at a zero word in BSS = silence. |
| ds:0xaf48 | frame counter value when last digitized sfx started (`3C8: mov ax,[0x160c]` / `3CB: mov [0xaf48],ax`). |

Startup order in main (0x1b8): `1F0: call 0x5a7c` (SB detect) → `1F3: call 0x58b1`
(OPL init) → `1F6: call 0x3ab0` (timer ISR install) → … → `202: call 0x571b` (cfg)
→ `205: push 0 / 207: call 0x57a8` (start song 0).

Shutdown fn_0046: text mode (0x5fdf(3)), `54: call 0x3b4f` restore timer,
`57: call 0x5889` silence OPL, `5A: call 0x5ba4` stop SB DMA, exit.

--------------------------------------------------------------------------------
## 1. Timer ISR (install 0x3ab0, ISR 0x3afb)

Install (0x3ab0): saves int8 vector (0:0x20) into the far-jmp at cs:0x3b4b; sets
int 8 -> cs:0x3afb and int 0x24 (critical error) -> cs:0x3af8 (`mov al,3; iret`
= always "fail"). PIT: `3ADE: mov al,0x36 / out 0x43` then divisor bytes
`0xE4,0x19` = 0x19E4 = 6628 → 1193182/6628 = **180.02 Hz**; PIT ch2 set to mode
0xB6 (square) divisor 2 (inaudible placeholder).

ISR 0x3afb (every 5.55 ms), DS loaded from cs:0x3aae (= 0x066E):
1. `3B03: call 0x5a39` — **music tick at 180 Hz** (every IRQ).
2. Phase byte [0x319e] counts 9→0. On phase 0 **or** 5 (`3B0D: cmp byte [0x319e],0x5`),
   i.e. 2 of every 10 IRQs = **36.0 Hz**:
   - `3B14: inc word [0x160c]` — global frame counter (36 fps).
   - PC-speaker streaming: `3B18: mov bx,[0xba0] / 3B1C: mov cx,[bx]`;
     if the word is nonzero the pointer advances (`3B22: add word [0xba0],0x2`),
     **a zero word parks the pointer** (end marker, stays silent).
     Then `3B27: add cx,0x2` and the result is written to PIT ch2
     (`3B2A: mov al,0xb6 / out 0x43`, lo, hi to port 0x42).
     So stored words are **PIT-divisor minus 2**; output frequency =
     1193182/(word+2) Hz, updated at 36 Hz; word 0 → divisor 2 (~596 kHz,
     inaudible = silence). The speaker gate (port 0x61 bits 0-1) is enabled by
     play_sfx and left on; it is only cleared at shutdown.
3. `3B36: dec byte [0x319e]`; if it underflows: reset to 9 and far-jmp to the
   saved BIOS int8 (1 of 10 = 18.0 Hz, keeps DOS clock right); otherwise EOI.

Restore (0x3b4f): old vector back, PIT ch0 divisor 0 (18.2 Hz), ch2 divisor 0,
`3B79: in al,0x61 / and al,0xfc / out 0x61` — speaker gate off.

--------------------------------------------------------------------------------
## 2. MUZAX music player (OPL2, AdLib port 0x388)

### 2.1 MUZAX.LZS file format

music_start = **0x57a8(n)**:
- `57B1: cmp [0xbc2],ax` — no-op if song n already playing; `57BA: call 0x5889` reset.
- opens "muzax.lzs" (ds:0xe1e), `57D5-57E4: lseek(handle, n*6, SEEK_SET)`
  (ax=6, `imul word [bp+0x4]`) → **directory entries are 6 bytes**:

  ```
  dir[n] : u16 file_offset      ; lseek target (57FA-580B)
           u16 n_instruments    ; [bp-4]
           u16 raw_size         ; [bp-2]
  ```
  Retail file: 20 slots (first_offset 0x78 / 6), songs 0..13 populated, 14..19 zeroed.
- sanity: `581F: cmp word [bp-0x2],0x3e80 / jnc fail(0xff)` — raw_size must be < 16000.
- `583F: call 0x6660` LZS-decompress raw_size bytes to **ds:0x54bc** (`5838: mov ax,0x54bc`).
- `5855: mov ax,[bp-0x4]` then four `shl ax,1` (= *16), `5863: add cx,ax` →
  **event stream starts at 0x54bc + n_instruments*16**; instrument records are
  16 bytes each at the start of the decompressed song.
- `5866: call 0x5a61(event_ptr, 0x54bc)`; `586F: mov [0xbc2],ax` record song #.

Decompressed song image:
```
song := instrument_record[n_instruments]   ; 16 bytes each (see 2.4)
        event_word[*]                      ; u16 LE stream (see 2.6)
```
Songs: 0 = title/intro (started by main 0x207 and intro 0x4586), 1 = menu
(0x4e41 main menu, 0x5174 gomenu, 0x4cfc settings-menu sound-on),
2..13 = in-game (main loop `2C5: add ax,0x2` after rand%12 → 0x57a8(2+rand%12)).

### 2.2 Driver state (DS)

| addr | use |
|---|---|
| 0x31a0 | instrument table base (0x54bc during songs; 0xc90 during init) |
| 0x31a2 | current event pointer |
| 0x31a4 | loop-point event pointer |
| 0x31a6 | shadow of OPL reg 0xBD (AM/VIB depth + rhythm + 5 drum key bits) |
| 0x31a7..0x31b1 | 11 bytes: current instrument # per logical channel (`5915: mov [di+0x31a7],ah`) |
| 0x31b2 | "sync flag" byte set by event op 7 — **never read anywhere** (only writes at 5A34/5A74) |
| 0xc8f | tick delay counter (byte; init 0) |

### 2.3 OPL register write — 0x5876

`opl_write(al=reg, ah=value)`, **dx must already be 0x388** (set at 5894/5A39):
`5876: out dx,al`; 6× `in al,dx` (index settle); `587D: mov al,ah / inc dx /
out dx,al / dec dx`; `5882: mov cx,0x23` + 35× `in al,dx` (data settle). All OPL
access in the whole game goes through this function (verified: only `mov dx,0x388`
sites are 0x5894 and 0x5A39; only callers/jmps are within 0x5876..0x5A39).

### 2.4 Logical channel model & operator tables

The driver exposes **11 logical channels**; OPL2 is put in **rhythm mode**:

| logical ch | role | op1 slot (0xc27) | op2 slot (0xc32) | 0xC0/freq index (0xc3d) | keyed by |
|---|---|---|---|---|---|
| 0-5 | melodic 2-op | 00 01 02 08 09 0a | 03 04 05 0b 0c 0d | 0..5 | B0+ch bit5 |
| 6 | bass drum (2-op) | 0x10 | 0x13 | 6 | 0xBD bit4 |
| 7 | snare | 0x14 | 0xff (none) | 7 | 0xBD bit3 |
| 8 | tom | 0x12 | 0xff | 0xff (no C0 write) | 0xBD bit2 |
| 9 | cymbal | 0x15 | 0xff | 8 | 0xBD bit1 |
| 10 | hi-hat | 0x11 | 0xff | 0xff | 0xBD bit0 |

Tables dumped from image.bin (DS+addr):
```
ds:0xc1c (reg group per instrument byte#): 20 40 60 80 e0 20 40 60 80 e0 c0
ds:0xc27 (op1 offset per channel):         00 01 02 08 09 0a 10 14 12 15 11
ds:0xc32 (op2 offset per channel):         03 04 05 0b 0c 0d 13 ff ff ff ff
ds:0xc3d (C0-reg / phys channel index):    00 01 02 03 04 05 06 07 ff 08 ff
```
0xc27/0xc32/0xc3d are contiguous, exploited by the volume code: indexing
0xc27[ch+11] reads 0xc32[ch] (`5A10: add bx,0xb` … `59E8: mov al,[bx+0xc27]`).

**Reset 0x5889**: event ptr = ds:0xcbc (built-in idle stream, §2.7);
[0x31a6]=0xE0; writes regs 0x40..0x55 all = 0x3F (`5897: mov ax,0x3f40` …
`58A1: cmp al,0x55`) = max attenuation on every operator TL; then key_off
channels 7..0 (`58A5: mov al,0x7` … `58AC: dec al / jns`).

**Init 0x58b1** (once at boot): reset; `58B4: mov ax,0x2001` reg 0x01=0x20
(wave-select enable); reg 0x08=0x00; `58C0: mov ax,0xe0bd` reg **0xBD = 0xE0**
(AM depth on, VIB depth on, **rhythm mode on**, drums off). Then programs default
percussion patches: `58C6: mov word [0x31a0],0xc90`, loop al=7..10 calling
0x58fd(ch=al, instr=0) with `58D4: add word [0x31a0],0xb` — i.e. **4 built-in
11-byte patches at ds:0xc90** for logical ch 7 (snare), 8 (tom), 9 (cymbal), 10 (hi-hat):
```
ds:0xc90: 0c 00 f8 b5 00 | 00 00 d6 4f 00 | 01   (snare)
ds:0xc9b: 04 00 f7 b5 00 | 00 00 d6 4f 00 | 01   (tom)
ds:0xca6: 01 00 f5 b5 00 | 00 00 d6 4f 00 | 01   (cymbal)
ds:0xcb1: 01 00 f7 b5 00 | 4e 00 10 00 00 | 01   (hi-hat)
```
Finally **0x58df fixed percussion pitches**: `58DF: mov ax,0xaca8` → reg
0xA8=0xAC; `58E5` reg 0xB8=0x0C; `58EB` reg 0xA7=0x02; `58F1` reg 0xB7=0x0D.
I.e. phys ch 8 fnum=0x0AC block 3 (~65.4 Hz) and phys ch 7 fnum=0x102 block 3
(~98.0 Hz) — the pitch sources for snare/hi-hat (ch7) and tom/cymbal (ch8).

> **Correction to FORMATS.md §8 / MAP**: these are NOT an "engine hum" and
> nothing retunes them with ship speed. Exhaustive search: no other writer of
> port 0x388/0x389, no other `call 0x5876`, and no gameplay calls into
> 0x58fd/0x5955/0x59b3/0x59f1 (grep of all call sites). SkyRoads has no
> speed-dependent engine sound.

### 2.5 16-byte instrument record & programming (0x58fd)

`program_instrument(al=channel, ah=instr#)` — also event opcode 1:
- `58FE: call 0x59b3` key_off(channel) first.
- `5902: mov si,ax / and si,0xff00 / shr si,4` → si = **instr# * 16**;
  `590B: add si,[0x31a0]` → record address. `5915: mov [di+0x31a7],ah` remembers
  the channel's instrument (needed by volume events).
- bytes 0-4 → op1: `591B: mov ah,[bx+si]` value, register =
  `591D: mov al,[di+0xc27] / 5921: add al,[bx+0xc1c]` (bx=0..4).
- bytes 5-9 → op2: register = `5930: mov al,[di+0xc32] + [bx+0xc1c]` (bx=5..9);
  `5938: jc 0x593d` **skips the write when 0xc32[ch]=0xFF** (0xff+0x20 carries)
  — single-op percussion channels ignore bytes 5-9.
- byte 10 → reg 0xC0+0xc3d[ch] (`5943: mov al,[di+0xc3d] / 5947: cmp al,0xff`
  skip if 0xff; `594B: add al,[bx+0xc1c]` with bx=10 → +0xC0).

```
instrument_record[16]:
  +0  op1 (modulator) reg 0x20  : AM/VIB/EG-sustain/KSR/MULT
  +1  op1 reg 0x40             : KSL(b7-6) | total level(b5-0)
  +2  op1 reg 0x60             : attack/decay
  +3  op1 reg 0x80             : sustain/release
  +4  op1 reg 0xE0             : waveform
  +5..9  op2 (carrier) same five registers
  +10 reg 0xC0                 : feedback(b3-1) | connection(b0)
  +11..15  padding, always 00  (verified across all 14 retail songs)
```
Byte 10 bit0 (connection) is also read by the volume handler
(`5A1F: test byte [si+0xa],0x1`): CNT=1 (additive) → volume applied to both ops.

### 2.6 Event stream — u16 words, opcode = bits 0-2

Dispatcher (tick) **0x5a39**, called at 180 Hz from the ISR:
```
5A39: mov dx,0x388
5A3C: cmp byte [0xc8f],0x0      ; delay pending?
5A43: dec byte [0xc8f] / ret    ;   yes: burn one tick
5A48: mov si,[0x31a2] / lodsw   ; fetch next u16 event
5A53: and bx,0x7 / shl bx,1     ; opcode = word & 7
5A58: shr al,byte 0x4           ; al = channel = (low byte) >> 4
5A5B: call word [bx+0xc67]      ; handler(al=channel, ah=high byte)
5A5F: jmp 0x5a3c                ; keep consuming until a delay starts
```
Encoding: `word = (param << 8) | (channel << 4) | opcode`. Bit 3 is always 0 in
retail data; channel nibble is 0 for opcodes 0/5/6/7 (verified over all songs).

Handler table at **ds:0xc67** (image bytes `f8 58 fd 58 55 59 b3 59 f1 59 26 5a 2d 5a 34 5a`):

| op | handler | meaning |
|---|---|---|
| 0 | 0x58f8 | **delay**: `58F8: mov [0xc8f],ah` — pause param ticks (1/180 s each); param 0..255, longer waits chained (e.g. `ff00 fb00` = 506 ticks). The tempo mechanism — there is no other timing. |
| 1 | 0x58fd | **set instrument**: channel ← instrument record # param (§2.5). |
| 2 | 0x5955 | **note on** (below). |
| 3 | 0x59b3 | **note off**: ch<6 → reg 0xB0+ch = 0x00 (`59B7: add al,0xb0 / xor ah,ah`); ch>=6 → clear drum bit: `59C2: mov ah,0xef / ror ah,cl` (cl=ch-6) AND into [0x31a6], write 0xBD. |
| 4 | 0x59f1 | **set volume** (below). |
| 5 | 0x5a26 | **jump to loop point**: `5A26: mov ax,[0x31a4] / mov [0x31a2],ax`. Every retail song ends with word 0x0005 → infinite loop; there is no "stop" opcode. |
| 6 | 0x5a2d | **set loop point** = current position (`5A2D: mov ax,[0x31a2] / mov [0x31a4],ax`). At stream start the loop point = song start (set by 0x5a61). |
| 7 | 0x5a34 | **set sync flag**: `5A34: mov [0x31b2],ah`. Written, never read by SkyRoads (leftover composer/engine feature). Appears once in retail data: song 0, value 99. |

playback_start **0x5a61(event_ptr, instr_base)**: [0x31a2]=[0x31a4]=event_ptr,
[0x31a0]=instr_base, [0x31b2]=0.

**Note on 0x5955** (al=channel, ah=note):
- key_off(ch) first (`5956: call 0x59b3`).
- `595F: cmp al,0x7` then `5961: mov al,[bx+0xc3d]` (physical channel);
  **ch >= 7** → `5967: jnc 0x599b` straight to the drum-bit path (note byte
  ignored — snare/tom/cymbal/hi-hat pitch is fixed by the 0x58df presets).
- ch <= 6: `596B: mov cl,0xc / 596D: div cl` → octave=note/12, semitone=note%12;
  `596F: add al,0x2` → **block = note/12 + 2**;
  reg 0xA0+phys: fnum low from `5978: mov ah,[bx+0xc77]`;
  reg 0xB0+phys: `5984: mov ah,[bx+0xc83]` | block<<2 (`5980: shl cx,2`),
  plus key-on bit 0x20 if reg < 0xB6 (`598C: cmp al,0xb6 / 5990: or ah,0x20`)
  — i.e. melodic ch 0-5 get key-on; ch 6 (bass drum, phys 6) gets its frequency
  written **without** key-on and falls through to the drum-bit path.
- drum-bit path 0x599b: `599C: mov cl,al / sub cl,0x6 / 59A1: mov ah,0x10 /
  59A3: shr ah,cl` → bit 0x10>>(ch-6) OR'd into [0x31a6], write reg 0xBD.

F-number tables (12 semitones C..B), from image:
```
ds:0xc77 (low byte):  ac b6 c1 cd d9 e6 f3 02 11 22 33 45
ds:0xc83 (high bits): 00 00 00 00 00 00 00 01 01 01 01 01
```
→ fnum = 0x0AC..0x145; freq = fnum * 49716 / 2^20 * 2^block. Note 0 = C with
block 2 ≈ 32.7 Hz (C1); retail songs use notes 21..60 (blocks 3..7).

**Volume 0x59f1** (al=channel, ah=volume 0..30):
- di=volume; si = instrument record of the channel's current patch
  (`59FA: mov si,[bx+0x31a7]` … `5A02: shl si,4 / add si,[0x31a0]`).
- If 0xc32[ch]==0xFF (single-op percussion): apply to op1 directly
  (`5A09: cmp byte [bx+0xc32],0xff / jz 0x59d3`).
- Else apply to op2 (carrier) via `5A10: add bx,0xb / 5A13: add si,0x5`, and
  additionally to op1 iff connection bit set (`5A1F: test byte [si+0xa],0x1`).
- Core 0x59d3: takes the patch's 0x40 byte (`59D3: mov ah,[si+0x1]`), keeps KSL
  bits, adds **attenuation table** value: `59DB: add ah,[di+0xc48]`, clamps to
  0x3F, writes reg 0x40+op (`59E8: mov al,[bx+0xc27] / 59EC: add al,0x40`).

Volume table ds:0xc48 (31 entries; volume value indexes added attenuation,
0 = silent, 30 = patch volume):
```
3f 14 10 0e 0c 0a 09 08 07 06 06 05 05 04 04 04
04 04 03 03 03 03 02 02 02 01 01 01 01 00 00
```

### 2.7 Idle stream & retail data facts

Built-in idle stream at **ds:0xcbc** (`5889: mov word [0x31a2],0xcbc`), bytes
`06 00 00 ff 05 00` = set_loop, delay 255, goto_loop — silent forever, keeps the
tick handler harmless when no song is loaded.

Retail MUZAX.LZS (validated by full decode of all songs):
14 songs, 7-9 instruments each, 1.5k-6.1k events; every song: optional intro →
`set_loop` → body → `goto_loop`; volume values span 0..30; channels 0-7,9,10
used (tom ch8 never used); songs embed copies of the default percussion patches
and assign them to ch 6/7/9/10 via opcode-1 events; instrument padding bytes
11-15 are 0 in all 124 records.

--------------------------------------------------------------------------------
## 3. PC-speaker SFX (no Sound Blaster present)

play_sfx (§4) speaker path (`45E: cli / 45F: in al,0x61 / or al,0x3 / out 0x61`
— gate speaker + PIT2) sets `46B: mov ax,[bx+0x132] / 46F: mov [0xba0],ax`:
**pointer table at ds:0x132**, 5 entries (one per sfx 0..4):
```
ds:0x132: f2 00 0e 01 14 01 18 01 24 01   -> streams at 0xf2 0x10e 0x114 0x118 0x124
```
Stream = u16 words (PIT divisor - 2), stepped at 36 Hz by the ISR (§1), 0-word
terminates (pointer parks, output inaudible). Retail streams (freq = 1193182/(w+2)):
```
sfx0 @0xf2 : 3a98 2328 4268 2af8 2710 2ee0 4268 4e20 61a8 1b58 1770 2af8 2328, 0
             (~79,132,70,107,119,99,70,60,48,170,199,107,133 Hz - 0.36s rumble)
sfx1 @0x10e: 0fa0 06a4, 0                  (298, 700 Hz blip)
sfx2 @0x114: 3a98, 0                       (79.5 Hz, one 36Hz-tick)
sfx3 @0x118: 1388 x5, 0                    (238.5 Hz, 0.14 s)
sfx4 @0x124: 01f4 03e8 01f4 03e8 012c 01f4, 0   (2376/1190/.../2538/2376 Hz warble)
```
The gate stays enabled after a sound; only fn_3b4f (shutdown) clears it.
Initial [0xba0] = 0x319C → a BSS zero word (parked/silent from boot).

There is also a dead two-tone beep **0x3bff** (PIT2 divisor 0x533 ≈ 896 Hz,
busy-wait 0x2400 loops, then 0x633 ≈ 752 Hz, wait, gate off) — no callers found.

--------------------------------------------------------------------------------
## 4. Digitized SFX — Sound Blaster, SFX.SND / INTRO.SND

### 4.1 Detection 0x5a7c
Scans [0xcc2] = 0x210,0x220,…,0x260 (`5AEC: add word [0xcc2],0x10` /
`5AF1: cmp word [0xcc2],0x260`): DSP reset via port+6 (write 1, ~8 PIT ticks,
write 0), poll port+0xE bit7 up to 0xF0 PIT ticks, expect 0xAA from port+0xA
(`5B08: cmp al,0xaa`). On success writes DSP cmd **0xD1** (speaker on)
(`5B18: mov al,0xd1 / out dx,al`). Failure → [0xcc2]=0.

### 4.2 Sample buffer
fn_00bb (trekdat loader) allocates the shared SND buffer:
`108: push word 0x7d64 / call 0x3ed8 / 111: mov [0x456c],ax` — **one 32100-byte
segment [0x456c]** (sized for the larger file, INTRO.SND). DMA page check:
`114-127: (seg+0x7D7)&0xF000 == seg&0xF000`, else free & realloc — guarantees
the buffer never crosses a 64 KB DMA page.

Loaders (whole file read raw into [0x456c]:0000 via 0x5f77(handle,off,seg,len)):
- SFX.SND (ds:0xe0c) — in load_cars fn_554b: `55D2: push 0x7d64 … 55DB: push 0xe0c`.
- INTRO.SND (ds:0xd72) — in intro fn_4575: `46E6: push 0x7d64 … 46F1: push 0xd72`.
The two files share the buffer; SFX.SND is (re)loaded whenever gameplay assets load.

### 4.3 SFX.SND format (offset table is INSIDE the file)
play_sfx reads `401: mov ax,[es:(n+1)*2]` minus `419: sub ax,[es:n*2]` = length,
and `430: mov ax,[es:n*2]` = offset; the **first byte of each record is the DSP
time constant**: `443: mov al,[es:bx]` pushed as arg3, buffer pointer +1 as the
DMA address (`450: add ax,0x1`).

```
SFX.SND := u16 offset[N+1]      ; from file start; offset[0] = 2*(N+1); offset[N] = file size
           record[N]            ; record = { u8 time_constant, u8 pcm[len-1] }
```
Retail file (25807 bytes): offsets 0xC,0xF9C,0x23BE,0x4353,0x4674,0x64CF → **5
samples** (8-bit unsigned mono):

| # | off | bytes | TC | rate | dur | used at (push n / call 0x3c2) |
|---|---|---|---|---|---|---|
| 0 | 0x000C | 3984 | 0x06 | 4000 Hz | 1.00 s | 0x1ACD, 0x2302, 0x27CB — explosion/crash |
| 1 | 0x0F9C | 5154 | 0x83 | 8000 Hz | 0.64 s | 0x247F (after waiting for current sfx via 0x476) — TBD (airborne, [0x9342]<0) |
| 2 | 0x23BE | 8085 | 0xEC | "50000 Hz" | — | 0x2744, 0x2781, 0x280E — TBD (TC 0xEC exceeds SB single-cycle spec; real DSPs clamp) |
| 3 | 0x4353 |  801 | 0x83 | 8000 Hz | 0.10 s | 0x13ED, 0x14D6 — fuel/oxygen gauge warning blip |
| 4 | 0x4674 | 7771 | 0x83 | 8000 Hz | 0.97 s | 0x1AF8 — speed-up/boost (when speed crosses 0x6978→0x7530) |

INTRO.SND (32100 bytes) has **no header at all**: raw 8-bit PCM played as one
shot by the intro: `4768: push 0x7d64 / 476B: push 0x5a / 4775: call 0x5b5a`
→ sb_play(off=0, seg=[0x456c], **TC=0x5A → 1000000/(256-90) ≈ 6024 Hz**,
len-1=0x7d64), ≈5.3 s.

### 4.4 play path
**fn_03c2 play_sfx(n)** (10 call sites, n = 0..4): records start frame in
[0xaf48]; aborts if [0x4528] (sound off); SB present → stop current
(`436: call 0x5ba4`), then `455: call 0x5b5a(buf_off+1, buf_seg, tc, len-1)`;
no SB → PC-speaker path (§3).

**0x5b1d dma_setup(dx=off, bx=seg, cx=count)**: mask ch (`out 0x0A, 4|ch`),
mode = (4|ch)^0x5C = **0x58|ch = single-cycle, read, auto-init, increment**
(`5B24: xor al,0x5c / out 0x0B`), clear flip-flop, 20-bit address
(`5B2C-5B36: ax=seg<<4+off, page=seg>>12+carry`), address+count to ch ports
(`5B3D: shl dx,1` → ports 2/3 for ch1), page reg from table ds:0xcc6,
unmask (`5B55: mov al,0x1 / out 0x0A` — hardcoded ch1 unmask).
Count programmed = cx-1 where caller passes len-1 → **DMA transfers len-1 bytes**
(the PCM after the TC byte).

**0x5b5a sb_play(off, seg, tc, len_minus_1)**: dma_setup; DSP writes (each
preceded by busy-wait `in al,dx; shl al,1; jc`): cmd **0x40** + tc
(`5B7E: mov al,0x40` / `5B86: mov al,[bp+0x8]`), cmd **0x14** (8-bit
single-cycle DMA output) + (len-2) lo/hi (`5B8F: mov al,0x14` /
`5B97: mov al,cl` / `5B9F: mov al,ch`). Rate = 1000000/(256-tc).

**0x5ba4 sb_stop**: DSP cmd 0xD0 (pause DMA) + mask DMA channel.

**fn_0476 sfx_playing()**: SB → `486: mov ax,[0xaf48] / add ax,0x8 /
48C: cmp [0x160c],ax` returns 1 within 8 frames (0.22 s) of start (crude timer,
no DSP IRQ is used); speaker → `4A4: mov bx,[0xba0] / cmp word [bx],0x0`
(stream not yet parked).

--------------------------------------------------------------------------------
## 5. skyroads.cfg — load 0x571b / save 0x5770

File = **66 (0x42) bytes**, image of ds:0x4524..0x4565
(`572D: push 0x42 / 572F: push 0x4524` read; same in save via write 0x606d).

```
+0x00 u16  checksum
+0x02 u16  control device: 0=keyboard 1=joystick 2=mouse   ([0x4526])
+0x04 u16  sound off flag: 0=on 1=off                      ([0x4528])
+0x06 u16[30]  completion counter per road 1..30           ([0x452a])
```
- Checksum fn_56c3: `sum(word[i] XOR i) for i=1..32` over the 32 payload words
  (`56E5: mov ax,[bx] / 56E7: xor ax,[bp-0x6] / 56EA: add [bp-0x4],ax`,
  loop `56F1: cmp word [bp-0x6],0x21`). Returns 0 if it equals stored [0x4524],
  else stores the new sum and returns 1. Load: nonzero → cfg invalid →
  `5759: push 0x42 / 0 / 0x4524 / call 0x5cc8` memset to zero. Save: recompute
  then write all 66 bytes; called after every completed road (`3A4: call 0x5770`).
- [0x4526] is copied to [0x9602] (input source for play_road) each menu pass
  (`232/296: mov ax,[0x4526] / mov [0x9602],ax`); [0x9602]=3 means demo playback
  (`229: mov word [0x9602],0x3`). Set from the settings menu: selection 0..2 →
  `4CD5: mov [0x4526],ax`; selections 3..4 → `4CDE: add ax,0xfffd /
  4CE1: mov [0x4528],ax`, then music stopped ([0xbc2]=0xFFFF) or song 1 started.
  Settings-menu highlight fn_4ae2 confirms: items 0-2 radio = [0x4526], items
  3-4 radio = [0x4528]+3 (SETMENU.LZS art: keyboard/joystick/mouse + speaker/Zzz).
- Completion: word[road] incremented (not just set) on completion
  (`39C: add word [bx],0x1` at 0x452a+road*2), then road++ and save. "The End"
  triggers when the 30th distinct road completes (`350: cmp si,0x1d`).
- **No key bindings in the file** — keys are a hardcoded scancode table at
  ds:0xba2 (`48 50 4b 4d 47 49 4f 51 01 39 19` = up,down,left,right,home,pgup,
  end,pgdn,esc,space,P; int9 ISR 0x3bb0 sets bit7 in-place on make).

--------------------------------------------------------------------------------
## 6. SPEED.DAT (and the shared .DAT loader)

**fn_5465(name, table_buf, n_words, farptr_slot)** — generic loader
(NOT a callback; 4th arg is a 4-byte far-pointer slot):
- filesize via lseek-end (`5484-548E`), rewind (0x60b0 = lseek 0,0),
- `54AB: call 0x3ed8` alloc (filesize - n_words*2) bytes → segment stored at
  slot+2, offset 0 at slot+0 (`54B4: mov [bx+0x2],ax / 54BA: mov word [bx],0x0`),
- read n_words*2 bytes into ds:table_buf (`54CB: call 0x5f3f`), rest of file
  into seg:0 (`54DB: call 0x5f77`).

Call sites (fn_54ec): oxy_disp.dat → table ds:0x9604, slot 0x5482, n=10;
ful_disp.dat → ds:0x961a, slot 0x548c, n=10; **speed.dat → table ds:0x4580,
slot ds:0x54b0, n=0x22 (34)** (`5514: push 0x54b0 / 5517: push 0x22 /
5519: push 0x4580 / 551C: push 0xde2`).

File format (3903 bytes, parse validated, identical record shape to
OXY/FUL_DISP):
```
u16 offset[34]                  ; relative to end of table (file pos 68)
record[34]:
  u16 screen_offset             ; y*320+x in mode 13h
  u8  width, u8 height
  u8  cell[w*h]                 ; values 0,1,2
```
These are the **34 incremental fill segments of the speedometer arc** (record 0
at (144,188) … record 33 at (176,189), sweeping over the dial top).
Consumer in fn_124b: steps the displayed value [0x41ca] one segment at a time
toward the target, calling fn_0edf for each step:
`12EB: mov ax,[bx+0x4580] / 12EF: mov cx,[0x54b0] / 12F3: mov dx,[0x54b2] /
12F7: add cx,ax / 12FB: call 0xedf(farptr, lit_flag)` with lit_flag=1 when
the gauge decreases (`12DD: mov ax,0x1` on the down direction).

fn_0edf draw_gauge_segment(rec_ptr, lit): cell values map 0→transparent,
1→color A, 2→color B; VGA colors lit ? (0x5E,0x5F) : (0x5C,0x5D)
(`4EF8: mov ax,0x5e` etc; dashboard palette range), EGA (0,5) / (0,8).
Cells are expanded into a scratch object (seg [0xaf3a]) by fn_0eb5, then
blitted to 0xA000 (and 0xA200 page on EGA) via fn_41e5.

Dead code: fn_4547(x,y,n) prepares object ds:0xbc4 (image init {seg=0,ofs=0,
h=8,w=8}) with w=n*8 — never called (no `call 0x4547` sites).

--------------------------------------------------------------------------------
## 7. Text renderer fn_450a

`fn_450a(x, y, str, color)` — `4510: mov bx,[bp+0x8] / 4513: mov al,[bx]` loop
until NUL, per char `4536: call 0x44a2(x, y, char, color)`, advance
`453C: add word [bp+0x4],0x8` → **8 px per character**.

Glyph source — **ROM 8x8 BIOS font, both halves** (fn_0000):
`23: mov ax,0x1130 / 26: mov bh,0x3 / int 10h` → es:bp saved to
[0x3194/0x3196] (8x8 font, chars 0-127); `35: mov bh,0x4` → [0x3198/0x319a]
(8x8 font, chars 128-255). There is **no 8x16 font** (corrects the task hint).

fn_44a2 draw_char: selects font ptr into [0x3190/0x3192]
(`44A8/44CF: mov [0x3190],ax`), chars >= 0x80 use the bh=4 half with char&0x7F
(`44B9: and ax,0x80` / `44C4: and word [bp+0x8],0x7f`). VGA → fn_3c36; EGA →
fn_3e6e with color forced to 0xF (`44F5: push word 0xf`).

fn_3c36 (VGA): si = char*8 + [0x3190] (`3C40: shl si,3 / 3C43: add si,[0x3190]`),
ds = font seg, es = 0xA000, di = y*320+x (`3C56: mov cx,0x140 / mul`); 8 rows:
`3C66: rol al,1 / jnc skip / 3C6A: mov [es:di],bl` — **only set bits drawn**
(transparent background), row stride `3C70: add di,0x138` (+312).

Usage: exactly two call sites, both in the post-game screen fn_2b21 —
"The End" (ds:0xd46) at (132,80) and "Road Completed" (ds:0xd4e) at (104,80),
color 0x63 (`2C6E: push 0x63 … 2C78/2C8A: call 0x450a`).

--------------------------------------------------------------------------------
## 8. Palette fade fn_4b72 (and helpers)

`fn_4b72(pal, fade_in, steps)`:
- `4B78: push 0x300 / push 0 / push 0x31b4 / call 0x5cc8` — memset 768-byte
  black palette in the stream-buffer scratch at ds:0x31b4.
- VGA: fn_4b27 snapshots both palettes into allocated segments and builds
  descriptors {seg, first_color=0, count=0x100} (`4B61: mov word [bx+2],0` /
  `4B69: mov word [bx+4],0x100`).
  fade_in!=0 → fn_4315(from=black, to=pal, steps); else fn_4315(from=pal,
  to=black, steps) (argument order at 4BB6 vs 4BCA).
- EGA: no fade — palette set instantly via 0x60c1 (pal or black, 4BED/4BF3).

**fn_4315(from_desc, to_desc, steps)** — frame-counter-driven linear DAC fade:
- `4328: mov word [0x160c],0x0`; each pass `4337: mov ax,0x64 /
  433A: imul word [0x160c] / 4340: div word [bp+0x8]` →
  **t = 100 * frames_elapsed / steps** (percent), clamped to 100; [0x54ac]
  (user-abort flag) forces t=100. fn_4137 drains DOS kbhit (int21 ah=0Bh/07h
  via 0x5f8e/0x5fad) and sets `4155: mov word [0x54ac],0x1` on any key, but
  only while armed ([0xaf42]!=0, e.g. `470D: mov word [0xaf42],0x1` for the
  intro).
- per byte (3 * count): `43F0: sub ax,cx / 43F2: imul word [bp-0x4] /
  43F5: mov cx,0x64 / 43F9: idiv cx` then `440A: add ax,cx` →
  **out = from + (to-from)*t/100**, into ds:0x31b4 (recomputed from the
  snapshots each pass, no drift).
- `4427: call 0x6233(first_color, count, far 0x31b4)` — DAC block write via
  ports 3C8/3C9 in 64-color bursts, waiting for vertical retrace between bursts
  (input-status port from BIOS [0:0x463]+6, `427B: test al,0x8`; falls back to
  int 10h ax=1012h if BIOS equipment byte & 6).
- repeats until t==100 → duration = steps frames at 36 Hz (steps=0x24=36 → 1.0 s,
  the standard call e.g. `2C98: push 0x24 / 2C9A: push 0 / 2C9C: push 0x41d0`).

fn_443d wait_frames(n): zero [0x160c], spin until >= n or [0x54ac] abort,
polling keys via fn_4137.

--------------------------------------------------------------------------------
## 9. ANIM.LZS container & intro sequence (fn_4575)

### 9.1 Container format (loader at 0x4629; parse of retail file validates)
```
ANIM.LZS := "ANIM"                       ; 4 bytes (0x4632/0x4635: two read_u16 discarded)
            u16 n_frames                 ; 0x4638: read_u16 -> [bp-0xe0]; retail: 100
            CMAP                         ; "CMAP" u8 n, 3n RGB + 2n EGA (0x4646: call 0x3fc6, base 0)
            frame[n_frames]:
              u16 n_picts                ; 0x465B: read_u16 (0 allowed = hold frame)
              PICT[n_picts]              ; standard PICT chunks (delta regions)
```
PICT chunk (exact, from fn_4068): `"PICT"` (4 bytes: `4071: call 0x64f0` skips
'PI', 'CT' lands in obj[0] and is overwritten by the pixel segment), then
u16 screen_offset, u16 height, u16 width, then an LZS block of w*h 8bpp pixels.
**Header is 10 bytes total — FORMATS.md's "u16 dummy" is actually the second
half of the tag.**

Loaded into a 10-byte/record table at ds:0x45c4: {u16 frame_index, obj{seg,
screen_ofs, h, w}} (`467F: mov [bx+0x45c4],ax` frame#, `4695: call 0x4068` into
+2), hard cap **300 records** (`46A0: cmp word [bp-0xdc],0x12c` → fatal error
0x40). Retail: 221 picts over 100 frames (1..27 per frame), pixels add color
base 0, share the single ANIM CMAP.

### 9.2 Intro flow (audio-relevant)
1. `4586: call 0x57a8(0)` — song 0 starts immediately.
2. INTRO.LZS: CMAP+PICT (base 0, title screen), CMAP+CMAP+PICT (base 0x32),
   7 x (CMAP, CMAP, PICT) at base 0xA0 — scene art with fade-from/fade-to
   palette pairs (descriptors fed to fn_4315).
3. ANIM.LZS loaded as above (0x4629-0x46D6).
4. INTRO.SND read raw into [0x456c] (0x46E6-0x4704).
5. Title: palette fade-in `474B: push 0x24 / 4750: call 0x4b72(0x5182,1,36)`,
   wait 0x18 frames; then **INTRO.SND played once**: `476B: push 0x5a /
   4775: call 0x5b5a(0, [0x456c], 0x5a, 0x7d64)` (~6 kHz, ~5.3 s) — PC-speaker
   users get nothing here. Skipped if [0x54ac] (user already pressed a key).
6. Animation playback: `47B1: cmp word [0x160c],0x2 / jnc` then reset — **one
   ANIM frame per 2 game frames = 18 fps**; all records whose frame_index ==
   current frame are blitted via fn_4293 (`47A5: cmp [bx+0x45c4],cx`). Abort →
   final title PICT blitted directly.
7. After the animation: 2 s hold, then on VGA a 319-step horizontal wipe paced
   by `484A: mov ax,0x13f / imul [0x160c] / 4851: mov cx,0x12 / div` (319 columns
   over 18 frames).

--------------------------------------------------------------------------------
## 10. Errata for existing docs (do not silently trust older notes)

1. FORMATS.md §8: MUZAX directory entries are **6 bytes** {offset, n_instruments,
   raw_size}, not 8; instrument patches are 16-byte records (11 used), not
   "11 bytes/patch at ds:0xc90" (0xc90 holds only the 4 built-in percussion
   patches, 11 bytes each).
2. FORMATS.md §8 / MAP.md: "channels 7-8 = engine hum, frequency follows speed"
   is wrong — 0x58df writes fixed rhythm-mode pitches; no speed-dependent OPL
   writes exist.
3. FORMATS.md §2: PICT header = "PICT" + u16 screen_offset + u16 h + u16 w
   (10 bytes); the "u16 dummy" listed there is the 'CT' tag half.
4. FORMATS.md §6: SPEED.DAT is **34 gauge-arc segment shapes** (same record
   format as OXY/FUL_DISP), not digit glyphs; text uses the BIOS ROM 8x8 font.
5. MAP.md "0x4526: cfg: last menu selection?" → it is the control-device
   setting (0=keyboard 1=joystick 2=mouse), copied into [0x9602] each menu pass.
