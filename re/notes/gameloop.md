# SkyRoads gameplay loop — fn_1f2c and friends, fully decoded

Source: re/full.asm (ndisasm of load image; DS = paragraph 0x66E, all `[0xNNNN]`
are DS-relative). Every constant below is quoted with the instruction address
that proves it. `shl/shr x,0x0` in quotes is the ndisasm quirk for shift-by-1.

Call graph covered here:

```
main 0x01b8
 └ play_road fn_2b21(last_road_flag)        ; per-road wrapper
    └ GAME LOOP fn_1f2c() -> result code
       ├ fn_0be3(sticky)   render orchestrator (ship sprite + viewport via fn_2d03)
       ├ fn_124b           dashboard update (speedo/fuel/oxy/progress/warnings)
       ├ fn_0e23           EGA page flip (int10 ah=5, page [0x9340])
       ├ fn_074c           input -> steer/accel/jump (kbd/joy/mouse/demo)
       ├ fn_04c0(z32,x)    tile lookup
       ├ fn_1a9c(code)     tile surface effects
       ├ fn_0533(z32,x,y)  "inside tunnel mouth" test (finish gate)
       ├ fn_0e58           completion fly-away, returns 0 (or 7 on ESC)
       ├ fn_17be(z32,x,y)  swept-collision move resolver
       │  └ fn_1685(z32,x,y) point/ship collision test
       │     └ fn_1584(code,t,y) per-tile block/tunnel profile test
       ├ fn_1d4d           jump autopilot (landing assist)
       │  └ fn_1c20 / fn_1bb5  trajectory sim / landing-cell safety
       └ fn_03c2(n)        play sfx n
```

Long-math helpers (verified at 0x5d0e/0x5d42/0x5e1c): fn_5d0e = 32x32→32
multiply (LXMUL), fn_5d42 = 32-bit shl by cl, fn_5d4e / fn_5e1c = 32/32
divides (5e1c shown negating operands = signed). Args pushed lo,hi,lo,hi.

--------------------------------------------------------------------------------
## 1. Ship / game state variables (DS-relative)

| var | addr | meaning | init (fn_1f2c prologue unless noted) |
|---|---|---|---|
| ship_x_0xaf2c        | 0xaf2c | ship X, 0x80 per screen pixel, 0x1700 per column | 0x8000 = center column 3 (`0x1f7e mov word [0xaf2c],0x8000`) |
| ship_y_0xaf3c        | 0xaf3c | ship height; road deck top = 0x2800 | 0x2800 (`0x1f84`) |
| ship_z_0x9628/0x962a | 0x9628 | 32-bit Z; **hi word = row index**, lo = fraction | row 3.0 (`0x1f72 [0x9628]=0`, `0x1f78 [0x962a]=3`) |
| speed_0x54b8/0x54ba  | 0x54b8 | 32-bit Z velocity per tick (z += speed) | 0 (`0x1f9a/0x1f9d`) |
| yvel_0x9342          | 0x9342 | vertical velocity per tick (y += yvel) | 0 (`0x1f96`) |
| xvel_0x4576          | 0x4576 | steering velocity, = steer * 0x1d | 0 (`0x1f93`) |
| side_push_0x54a2     | 0x54a2 | edge-slide X push per tick (added to X) | 0 (`0x1f8a`) |
| steer_0x9600         | 0x9600 | input: -1/0/+1 (left/right) | set by fn_074c |
| accel_0x933c         | 0x933c | input: -1/0/+1 (brake/accelerate) | set by fn_074c |
| jumpkey_0x5488       | 0x5488 | input: space held (0/1) | set by fn_074c |
| grav_accel_0x54b6    | 0x54b6 | per-tick gravity = -(g*0x1680/0x190) | see §5 |
| road_gravity_0x456e  | 0x456e | road header word 1 ("gravity" g) | load_road fn_55f8 |
| fuel_0x54a0          | 0x54a0 | fuel, 0..0x7530 (30000) | 0x7530 (`0x1fa1`) |
| oxy_0xb14c           | 0xb14c | oxygen, 0..0x7530 (30000) | 0x7530 (`0x1fa7`) |
| fuel_range_0x54ae    | 0x54ae | road header word 2 = fuel range in ROWS | load_road |
| oxy_secs_0x4574      | 0x4574 | road header word 3 = oxygen supply in SECONDS | load_road |
| rows_0x41ce          | 0x41ce | road row count (= rawsize/14, load_road return; stored `0x0274`/`0x02dc mov [0x41ce],ax`) | main |
| end_state_0x457c     | 0x457c | 0 alive; 1 wall-crash; 2 burned; 3 fell off; 4 out of fuel; 5 out of oxygen | 0 (`0x1fad`) |
| expl_ctr_0x4578      | 0x4578 | explosion frame counter, 1..42 while playing, ++ per tick | 0 (`0x1fb9`) |
| end_frames_0x4566    | 0x4566 | ticks elapsed since end_state became nonzero | 0 (`0x1fb3`) |
| ap_delta_0xaf3e/40   | 0xaf3e | 32-bit speed delta applied by autopilot (reverted on landing) | 0 (`0x1fe1`) |
| ap_light_0x4568      | 0x4568 | autopilot-engaged dashboard indicator | 0 (`0x1fcf`) |
| autopilot_on_0x457e  | 0x457e | landing-assist enable; hardcoded 1 in main (`0x01f9 mov word [0x457e],0x1`), not in skyroads.cfg (cfg = 0x42 bytes -> ds:0x4524, fn_571b 0x572d) | 1 |
| frame_ctr_0x160c     | 0x160c | 36 Hz tick counter (PIT ISR; see FORMATS §9) | 0 (`0x1fe8`) |
| dead_ctrls_0x41cc    | 0x41cc | "controls dead" flag — **read only** (0x2281, 0x22b0); no writer exists; BSS ⇒ always 0 (dead code) | 0 |
| dead_frames_0x9620/22| 0x9620 | 32-bit tick count while dead_ctrls!=0 (dead path) | 0 (`0x1ff7`) |
| demo_ptr_0x9628      | —      | demo index derives from ship Z (see §10), bufs 0x962e |  |
| sfx_tick_0xaf48      | 0xaf48 | tick when last sfx started (fn_03c2 `0x3c8/0x3cb`) | 0 (`0x1ffe`) |
| dash caches          | 0x41ca speedo segs, 0x9618 fuel segs, 0x457a oxy segs, 0x456a progress ticks, 0xaf2e blink phase, 0xaf30 ap-light state | play_road `0x2b40..0x2b55` zeroes, 0xaf30=0xffff |
| page_0x9340          | 0x9340 | EGA page toggle (`0x0e14 xor word [0x9340],1`) | 0 (play_road 0x2b55) |
| vga_backseg_0x5486   | 0x5486 | VGA back buffer segment (0xAC80 bytes, main `0x0305`) | main |

fn_1f2c locals (bp-relative, names used below): `[bp-0x2]` ticks_done,
`[bp-0x4]` tick_at_frame_start, `[bp-0x6]` ap_done_this_jump, `[bp-0x8]`
jumping, `[bp-0xa]` jump_start_y, `[bp-0xc]` on_ground, `[bp-0xe]` on_ice,
`[bp-0x10]` on_sticky, `[bp-0x12]` over_hole, `[bp-0x14]` tile_code,
`[bp-0x16]/[bp-0x22]/[bp-0x18]` edge-scan i/balance/edge_dist,
`[bp-0x1a]` new_x, `[bp-0x1c]` new_y, `[bp-0x20]/[bp-0x1e]` new_z32,
`[bp-0x24]/[bp-0x28]/[bp-0x26]` temps.

--------------------------------------------------------------------------------
## 2. play_road fn_2b21(is_last_road) wrapper

- `0x2b27..0x2b34`: [0xaf3a] = segment of scratch buffer ds:0x31b4 (gauge blits).
- `0x2b37 call 0x3b81`: clear key states + install int9 ISR (key cells ds:0xba2,
  scancodes 48 50 4b 4d 47 49 4f 51 01 39 19 = up,down,left,right,home,pgup,
  end,pgdn,ESC,SPACE,P; bit7 = held — verified bytes at ds:0xba2).
- zeroes dash caches; copies pristine dashboard to screen (rows 138+).
- `0x2ba7..0x2bbd`: draws **gravity display = ([0x456e]-3)*100**, 4 digits at
  (96,156): `0x2ba9 mov ax,[0x456e]; 0x2bac add ax,0xfffd; 0x2baf mov cx,0x64;
  0x2bb2 imul cx; push 0x9c; push 0x60; call 0x1067`.
- `0x2bea..0x2c32`: draws initial ship sprite; sprite index formula
  `((fn_0afa(0,0x2800)*3 + fn_0b34(0x8000))*3 + 14) * 0x2d0` — see §11.
- `0x2c35 call 0x4b72(0x41d0,1,0x24)`: 36-step palette fade-in.
- `0x2c42 call 0x1f2c` → result in si.
- result==0: `0x2c62 call 0x3c7e` then prints string at ds:0xd46 ("road
  complete...", arg!=0 variant) or ds:0xd4e at (0x84|0x68,0x50), waits key
  fn_443d(0x1b); fade-out; returns result.
- main post-game (`0x0388..0x03b4`): result 0 → completion flag
  `[0x452a+road*2] += 1`, road++ ([0x933e]), save cfg fn_5770; result 7 → menu;
  results 1..5 → replay same road.

--------------------------------------------------------------------------------
## 3. fn_1f2c structure, timing, frame skip

Initialization quoted in §1 plus:
- `0x1f32`: if [0x9602]==2 (mouse) center mouse: fn_06a4(0xa0,0x64) =(160,100).
- gravity precompute `0x1f4d..0x1f6f`:
  `mov ax,[0x456e]; ... cx=0x1680; call 0x5d0e (g*0x1680); cx=0x190;
  call 0x5d4e (÷0x190); neg ax; mov [0x54b6],ax`
  ⇒ **grav_accel = -(g * 0x1680 / 0x190) = -(g*5760/400) = -14.4*g** per tick
  (integer; g=8 → -115).

Loop skeleton:

```
0x200d top:    if [0x160c] < ticks_done: both = 0     ; ISR counter reset guard
0x2025         tick_at_frame_start = [0x160c]
0x202b         pause: if P held (0xbac):
                 0x203c wait P release
                 0x2050 wait any key down (scans 0xba2..0xbac)
                 0x210e if ESC held -> return 7        ; 0x211f mov ax,7
                 0x2125 wait all keys released, restore [0x160c]
0x21e9         if ESC (0xbaa) held -> return 7         ; 0x21fa mov ax,7
0x2200         exit checks (see §4)
0x224c         call fn_0be3(on_sticky)                 ; render frame
0x2255         call fn_124b                            ; dashboard
0x225b         spin while [0x160c] == tick_at_frame_start   ; 36 Hz frame wait
0x2267         call fn_0e23                            ; EGA: page flip
0x2274         EGA only: call fn_5fc8
0x2277..0x229d input: if not demo: if [0x41cc]==0 call fn_074c
                       else zero [0x5488],[0x9600],[0x933c]
0x2ae1 bottom: while ticks_done < [0x160c]: run PHYSICS STEP 0x22a3 (ticks_done++)
0x2aec         jmp top
```

Physics thus runs exactly once per 36 Hz tick; if rendering takes >1 tick the
inner loop at `0x2ae1 cmp [bp-0x2],ax / 0x2ae9 jmp 0x22a3` executes multiple
physics steps without re-rendering (frame skip). Input is sampled once per
*rendered* frame in normal play, once per *physics step* in demo mode
(`0x22a3 cmp word [0x9602],0x3; 0x22ad call 0x74c`).

--------------------------------------------------------------------------------
## 4. Exit conditions and return codes

`0x2200..0x224c`, evaluated before each rendered frame:

```
if expl_ctr != 0 and expl_ctr <= 0x2a (42):  keep playing (explosion anim)
else if end_state == 1: return 1             ; 0x2214
else if end_state == 2: return 2             ; 0x221e
else if end_state == 3 and expl_ctr != 0: return 3   ; 0x2228..0x2239
else if end_frames_0x4566 >= 0x6c (108): return end_state  ; 0x223c cmp ...,0x6c
```

- end_frames increments only while end_state!=0 (`0x2ac9 add word [0x4566],1`)
  ⇒ death screens last 108 ticks = exactly 3.0 s before returning.
- ESC → **7** (0x211f pause path, 0x21fa direct).
- Completion → **0** via fn_0e58 (§9); fn_0e58 can itself return 7 on ESC.
- Return value consumed by main: 0 complete, 7 quit, 1..5 retry (death cause
  only affects sounds/anim, not flow).

Death states set at:
- **1 wall crash**: z move blocked && speed >= 0xe38 (`0x27ae cmp word
  [0x54b8],0xe38`) → `0x27c3 [0x4578]=1`, sfx 0, `0x27db [0x457c]=1`.
- **2 burned**: burning tile (fn_1a9c case 0xc, `0x1ab5 mov word [0x457c],2`,
  expl_ctr=1, sfx 0). (Also a dead code path `0x22f4` would set 2 after
  0x90=144 ticks of dead_ctrls!=0 — unreachable, [0x41cc] never written.)
- **3 fell off**: ship_y < 0x2800 (`0x2a95 cmp word [0xaf3c],0x2800; jc` →
  `0x2aa0 [0x457c]=3`).
- **4 out of fuel**: fuel==0 → `0x2ab0 [0x457c]=4`.
- **5 out of oxygen**: oxy==0 → `0x2ac0 [0x457c]=5`.

While end_state!=0 the speed/steer/jump update block is skipped
(`0x249e cmp word [0x457c],0; jnz → 0x2590`), but gravity/motion continue,
and fuel/oxy drain stops (`0x2a19` gate).

--------------------------------------------------------------------------------
## 5. Physics step (0x22a3..0x2add) in exact order

1. (demo only) fn_074c.
2. dead_ctrls path (unreachable, see §4).
3. **Tile under ship** `0x2308`: tile_code = fn_04c0(z_lo,z_hi,ship_x);
   over_hole = (tile_code==0) (`0x231d`).
4. **Surface resolution** `0x2332` — only if on_ground:
   - if ship_y > 0x2800 (`0x233b cmp word [0xaf3c],0x2800; ja`): standing on a
     block: `0x2346 bx=tile>>8; shl bx,1; 0x234e mov ax,[bx+0xde]` — block-top
     height table ds:0xde = {0x2800,0x3200,0x3200,0x3200,0x3c00,0x3c00} for
     high byte 0..5 (verified in image.bin at DS+0xde). If ship_y equals the
     table height: `0x235b mov cx,4; shr word [bp-0x14],cl` ⇒ **effective
     surface nibble = bits 4-7** (block-top surface). Else tile_code := 0.
   - fn_1a9c(tile_code) → surface effects (§8).
   - on_ice = ((code&0xf)==8) (`0x2372..0x2389`), on_sticky = ((code&0xf)==2)
     (`0x238c..0x23a3`). If airborne: on_sticky=0 (`0x23a9`); on_ice keeps its
     previous value while airborne.
5. **Finish check** `0x23ae`: if z32 >= ((rows_0x41ce-1)<<16) + 0x8000
   (= (rows-0.5) rows; `0x23ba add ax,0x8000; adc dx,-1` after shl 16):
   if fn_0533(z,x,y)!=0 (inside tunnel-gate profile, §9) and end_state==0 →
   `0x23ff call 0xe58; 0x2402 jmp exit` — returns fn_0e58's value (0).
6. **Landing aftermath / bounce** `0x2405` (uses previous step's new_y):
   if ship_y != new_y (vertical move got clamped):
   - if side_push!=0 && edge_dist<2 → yvel=0 (`0x2411..0x2421`).
   - else if |yvel| >= 0x104*g/8 (`0x243c mov ax,0x104; imul [0x456e];
     cx=8; div`) and expl_ctr==0:
       - if end_state==0 and yvel<0 and no sfx playing (fn_0476) → sfx 1
         (bounce thud, `0x247d`).
       - **bounce: yvel = -yvel*5/10** (`0x2485 imul ax,[0x9342],0x5; neg;
         cx=0xa; idiv`), i.e. 50% restitution.
   - else yvel = 0 (`0x2498`).
7. **Speed from input** `0x24a8` (skipped if end_state!=0):
   speed32 += accel_input * 0x4b (`0x24ac mov cx,0x4b; call 0x5d0e;
   0x24b9 add [0x54b8],ax; adc [0x54ba],dx`), clamp to **[0, 0x2aaa]**
   (`0x24f8 cmp word [0x54b8],0x2aaa`, sets 0x2aaa at `0x2503`).
   ⇒ accelerate/brake = ±75/tick; max speed 0x2aaa = 10922 ≈ 1/6 row/tick
   = 6.0 rows/s; 0→max in ~146 ticks ≈ 4.05 s.
8. **Steering** `0x250f`:
   - on_ice → keep previous xvel (no update).
   - else if (not jumping and not over_hole) → xvel = steer * 0x1d
     (`0x254c imul ax,[0x9600],0x1d; 0x2551 mov [0x4576],ax`) = ±29.
   - else (airborne): allowed only if xvel==0 && yvel>0 && (ship_y -
     jump_start_y) < 0xf00 (`0x253e..0x2547 cmp ax,0xf00`) — i.e. you can set
     a direction once, early in the ascent (first 3840 height units).
9. **Jump** `0x2554`: if !jumping && !over_hole && jumpkey && g < 0x14
   (`0x2570 cmp word [0x456e],0x14; jc`):
   **yvel = 0x480** (`0x257a mov word [0x9342],0x480`), fuel cost literally
   zero (`0x2580 sub word [0x54a0],0x0` — disabled cost), jumping=1,
   jump_start_y = ship_y. (Roads with gravity >= 20, e.g. road 20, cannot
   jump.)
10. **Autopilot trigger** `0x2590`: if autopilot_on && jumping &&
    !ap_done_this_jump && ship_y >= 0x3700 → fn_1d4d (§7), ap_done=1.
11. **Gravity** `0x25bf`:
    - if expl_ctr==0:
      - ship_y >= 0x2800 → yvel += grav_accel (`0x25d4 mov ax,[0x54b6];
        0x25d7 add [0x9342],ax`).
      - ship_y < 0x2800 (falling into the void) → yvel = min(yvel, -0x6a)
        (`0x25de cmp word [0x9342],0xff96 (-106)`, force -106 if slower).
    - else (exploding): yvel=max(yvel,0); if yvel<0x47: yvel += 0x27 else
      yvel=0x47 (`0x25f1..0x2613`) — wreck floats up ~71/tick.
12. **Integrate** `0x2619`:
    - new_z32 = z32 + speed32.
    - eff = speed32 + (on_sticky ? 0x618 : 0) (`0x2646 mov ax,0x618`).
    - new_x = ship_x + xvel*eff/0x200 + side_push
      (`0x265a..0x2681`: lmul xvel*eff, ldiv 0x200, add [0xaf2c], add
      [0x54a2]). At max speed: 29*10922/512 = 618 x-units/tick ≈ 4.8 px.
      Sticky floor guarantees lateral mobility ≥ 29*0x618/0x200 = 87.
    - new_y = ship_y + yvel (`0x2684`).
    - unsigned wrap guards `0x268e..0x26bb`: if x<0x2f80 && new_x>0xd080 or
      x>0xd080 && new_x<0x2f80 → new_x=x. (0x2f80/0xd080 = playfield x range,
      cf. fn_04c0.)
13. **Collision resolve** `0x26be call fn_17be(new_z,new_x,new_y)` — moves the
    globals as far toward the target as geometry allows (§6).
14. **Blocked-in-z handling** `0x26d0..0x2787`: if z fully applied → skip. If z
    blocked but x applied (clipped a wall corner): probe fn_1685(new_z, x, y);
    if solid, try x-0x3a0 and x+0x3a0 (`0x2717 add ax,0xfc60`,
    `0x2754 add ax,0x3a0`): if free, **nudge x by ∓0x3a0 (928)**, cancel the z
    move, sfx 2 (`0x2742/0x277f push 2; call 0x3c2`).
15. **Wall hit** `0x2787..0x2820`: if z still != new_z:
    - speed >= 0xe38 (3640 ≈ 1/3 max ≈ speedo 11/34) → explosion + death 1
      (see §4).
    - else: if the ship actually advanced this tick (resolved z strictly >
      new_z - speed = pre-move z, `0x27e4..0x2809`) → sfx 2 (bump).
      **speed32 = 0** (`0x2814`).
16. **Blocked-in-x** `0x2820..0x28bb`: if x != new_x: xvel=0 (`0x282c`);
    if side_push pushes toward the wall → side_push=0 (`0x2832..0x285c`);
    **speed32 -= 0x97** (151, wall-scrape friction `0x2862 sub word
    [0x54b8],0x97; sbb`), clamp [0,0x2aaa].
17. **Landing** `0x28bb..0x2a08`: on_ground=0; if y blocked && yvel<0:
    - ap_light=0, ap_done=0, jumping=0, on_ground=1 (`0x28d6..0x28e5`).
    - **speed32 -= ap_delta_0xaf3e/40** (`0x28f1 sub [0x54b8],ax; sbb`),
      clamp [0,0x2aaa]; ap_delta=0 — reverts the autopilot speed correction.
    - **edge-balance scan** `0x2953..0x2a08`: probe fn_1685(z, x ± i*0x80,
      y-1) for i=1..0xe (`0x2999 cmp word [bp-0x16],0xe`, `0x296a shl ax,7`):
      first free probe right → balance+1, first free left → balance-1, record
      edge_dist=i. If balance!=0: **side_push += balance*0x11**
      (`0x29f7 imul ax,[bp-0x22],0x11; 0x29fb add [0x54a2],ax`) — ship slides
       toward the unsupported side, accelerating 17/tick²; else side_push=0.
18. `0x2a08`: if ship_y > 0x7fff (wrapped negative) → ship_y=0.
19. **Fuel & oxygen** `0x2a19` (only while end_state==0):
    - oxygen: oxy -= 0x7530/(0x24 * oxy_secs) (`0x2a29 mov ax,0x24;
      imul [0x4574]; 0x2a32 mov ax,[bp-0x24]=0x7530; div cx;
      0x2a39 sub [0xb14c],ax`); clamp underflow to 0 (`0x2a48`).
      ⇒ full tank lasts exactly 36*oxy_secs ticks = **word3 seconds**.
    - fuel: fuel -= (0x7530/fuel_range) * speed32 / 0x10000
      (`0x2a4e div word [0x54ae]`, lmul speed, ldiv 0x10000 via bx:cx=1:0,
      `0x2a76 sub [0x54a0],ax`); clamp 0.
      ⇒ full tank lasts exactly **word2 rows** of forward travel.
      (Retail check: word2 >= row count for all 31 roads except those with
      supplies tiles, e.g. road09 50<70, road00 130<160.)
20. **Death checks** (§4) / end_frames++ / expl_ctr++ (`0x2ad8`), ticks_done++.

--------------------------------------------------------------------------------
## 6. Collision system

### fn_04c0(z_lo, z_hi, x) → u16 tile code  (0x4c0)
```
col_index si = x/0x80 - 0x5f (95)        ; 0x4c9 cx=0x80 div, 0x4d2 add 0xffa1
if si >= 0x142 (322) return 0            ; 0x4d7 — x valid range [0x2f80,0xd080)
row = z32 / 0x2000 / 8 = z >> 16         ; 0x4e0..0x50a
return word [0x1638 + row*0xe + (si/0x2e)*2]   ; 0x4ed mul 0xe, 0x517 div 0x2e
```
Geometry: 7 columns, **column width 0x2e (46) px = 0x1700 (5888) x-units**,
road spans x = 0x2f80..0xd080 (41216 units = 322 px), one row = 0x10000 z.
Ship starts at x=0x8000 = exact center of column 3.

### fn_1685(z_lo, z_hi, x, y) → 1 = solid (0x1685)
```
right = fn_04c0(z, x+0x700); left = fn_04c0(z, x-0x700)   ; 0x1693/0x16a8
; ±0x700 (1792) = ship half-width (14 px)
A) road deck: if (right|left)&0xf:                         ; 0x16bb..0x16d2
     if y < 0x2800 and y+0x600 > 0x2480 (⇔ y > 0x1e80): return 1   ; 0x16d7..0x16ed
B) if y+0x680 <= 0x2800 (⇔ y <= 0x2180): return 0          ; 0x16f3 (below world)
C) if (left&0xf00)==0 and (right&0xf00)==0: return 0       ; 0x1700 (no blocks)
D) center = fn_04c0(z,x); t = 0x17 - ((x/0x80 - 0x31) % 0x2e)  ; 0x172c..0x1744
   xoff=-0x1700; if t<=0: t = 1-t, xoff=+0x1700            ; 0x1747..0x176d
   ; t = 1..23 distance (px) from own column center, xoff → nearest neighbor
   if fn_1584(center, t, y): return 1                      ; 0x1770
   if fn_1584(fn_04c0(z,x+xoff), 0x2f - t, y): return 1    ; 0x1785..0x17a0
   return 0
```
So the road deck is a slab solid for 0x1e80 < y < 0x2800 under any tile whose
surface nibble (bits 0-3) is nonzero within ±0x700 of the ship; the ship is a
point vs. blocks but block tests extend into the neighbor column (index
0x2f-t = 24..46), which implements the ship's width against block sides.

### fn_1584(tile, t, y) → 1 = solid (0x1584)
```
if t > 0x25 (37): return 0                                ; 0x158d
di = (y - 0x2200) / 0x80                                  ; 0x159b add 0xde00
switch tile & 0xf00:                                      ; 0x1656 dispatch
 0x100 tunnel:        solid iff inner[t] <= di < outer[t]     ; 0x1629..0x164d
 0x200 half block:    solid iff y < 0x3200                    ; 0x15b3
 0x300 half+bore:     solid iff y < 0x3200 and di >= inner[t] ; 0x15df
 0x400 full block:    solid iff y < 0x3c00                    ; 0x15c9
 0x500 full+bore:     solid iff y < 0x3c00 and di >= inner[t] ; 0x1604
 else: return 0
```
Profile tables (word arrays, verified in image.bin):
- inner ds:0x46 (38 entries): 10 10 10 10 0f 0e 0d 0b 08 07 06 05 03 03 03 03
  03 03 02 01 00 00 00 00 00 00 01 02 03 03 03 03 03 03 05 06 07 08 (hex)
- outer ds:0x92 (38 entries): 20×17, 1f 1f 1f 1f 1f 1e 1e 1e 1d 1d 1d 1c 1b
  1a 19 18 16 14 12 11 0e (hex)
Tunnel interior is free below y = 0x2200 + inner[t]*0x80 (center: di<0x10 ⇒
y<0x2a00, i.e. 0x200 of headroom over the deck); the shell top at the center
is di=0x20 ⇒ y=0x3200, so you can land on and ride a tunnel roof.

### fn_17be(new_z32, new_x, new_y) — swept move (0x17be)
1. no-op if target == current (`0x17c4..0x17f4`).
2. coarse sweep si=1..5 (`0x186a cmp si,0x5`): test fn_1685 at current +
   delta*si/5 for all three axes (`0x1806 cx=5 idiv`, lmul/ldiv for z); stop
   at first solid fraction.
3. commit position at fraction (si-1)/5 (`0x1872..0x18d0`).
4. z refine `0x18d4..0x1965`: step starts 0x1000, advance z by step while the
   probe at z+step is free and step <= remaining; on failure step /= 0x10
   (`0x193d mov ax,0x10 ... call 0x5d4e`) — i.e. 4096/256/16/1 units.
5. x refine `0x1968..0x19f8`: step ±0x7d toward target while free; on failure
   step = step/5 (`0x19ee mov cx,0x5; idiv`) — 125/25/5/1.
6. y refine `0x1a00..0x1a90`: same ±0x7d, /5.
Globals 0x9628/0x962a, 0xaf2c, 0xaf3c are updated in place; caller compares
against the requested target to detect "blocked" per axis.

--------------------------------------------------------------------------------
## 7. Jump autopilot — fn_1d4d, fn_1c20, fn_1bb5

fn_1c20(z32, x, y, speed32, xvel, yvel) → 1 = jump lands safely (0x1c20):
simulates ticks: yvel += grav (`0x1c3b`), z += speed, x += side_push +
xvel*(speed+0x618)/0x200 (`0x1c56 add cx,0x618` — sticky bonus applied
unconditionally in the sim), abort 0 if x leaves [0x2f80,0xd080]
(`0x1c8b/0x1c94`), y += yvel, speed += accel_input*0x4b clamped [0,0x2aaa]
(`0x1cb6/0x1cf1`), until y <= 0x2800. Then safe iff fn_1bb5 approves both the
final position and the position one tick earlier (`0x1d0e/0x1d25`).

fn_1bb5(z32, x) → 1 = BAD cell (0x1bb5): code=fn_04c0; type=code&0xf00;
type==0x100 (tunnel) → good (`0x1be4` — you land on its roof); other types
shift code>>=4 (block top surface); bad iff surface nibble==0 with no block
(empty = hole) or surface nibble==0xc (burning) (`0x1bf5..0x1c16`).

fn_1d4d() (0x1d4d): if fn_1c20(current state) safe → return. Else for
strength di=1..6 (`0x1efe cmp di,0x6`):
1. xvel' = xvel + xvel*di/10 (`0x1d9f imul di; cx=0xa idiv; add ax,si`) → sim;
   keep if safe.
2. xvel' = xvel - xvel*di/10 (`0x1de6`) → keep if safe.
3. restore xvel; speed' = speed + speed*di/10 (`0x1e1c..0x1e3f`), only if
   < 0x2aaa (`0x1e55`) → keep if safe.
4. speed' = speed - speed*di/10 (`0x1e91..0x1ec7`) → keep if safe.
On exit: ap_delta_0xaf3e = final speed - original (`0x1f06..0x1f16`; 0 if all
attempts failed because speed is restored at `0x1eee`), and if a fix was found
(di<=6) ap_light_0x4568=1 (`0x1f22`) → dashboard cell redrawn by fn_124b.
The speedometer displays speed - ap_delta (original), and landing reverts the
speed change (§5.17) — the assist is invisible to the player's speed.

--------------------------------------------------------------------------------
## 8. Tile code format and surface effects

Tile word (7 per row at ds:0x1638, row 0 = start; ship spawns at row 3):

```
bits 0-3   ground surface type/color (0 = void; with bits8-11==0 and ==0 the
           whole word 0 = hole)
bits 4-7   block-top surface type/color (active when standing exactly at the
           block height from table ds:0xde)
bits 8-11  block geometry: 0 none, 1 tunnel, 2 half block (top 0x3200),
           3 half block with tunnel bore, 4 full block (top 0x3c00),
           5 full block with tunnel bore   (tunnel tops ride at 0x3200)
bits 12-15 unused (0 in all 31 retail roads; histogram over ROADS.LZS)
```

Heights: deck top y=0x2800, half-block top 0x3200 (+0xa00), full-block top
0x3c00 (+0x1400). Jump apex from flat ground ≈ 0x480²/(2*14.4g): g=8 →
+5765 > 0x1400 — a standard jump just clears a full block on gravity-8 roads.

Surface effects fn_1a9c(code) (0x1a9c), switch on code&0xf (`0x1b40`):
- **0x2 sticky**: speed32 -= 0x12f (303) per tick (`0x1b17 sub word
  [0x54b8],0x12f; sbb`). Also fn_1f2c gives lateral-move bonus +0x618 (§5.12)
  and the ship is drawn 0x80 lower (fn_0be3 `0xd2e add ax,0xff80`).
- **0x8 slippery (ice)**: no effect here; fn_1f2c freezes xvel updates (§5.8).
- **0x9 supplies**: fuel=oxy=0x7530 (`0x1afe/0x1b04`); sfx 4 only if either
  was below 0x6978 (27000) (`0x1ae0/0x1aeb cmp ...,0x6978`).
- **0xa boost**: speed32 += 0x12f (`0x1b2f add word [0x54b8],0x12f`).
- **0xc burning**: death code 2 + explosion + sfx 0 (`0x1ab5..0x1acd`).
- all others: plain color, no effect. Finally clamp speed [0,0x2aaa]
  (`0x1b63..0x1bb1`).
Effects only run while on_ground (§5.4); boost/slow are suppressed during the
explosion (gates at `0x1b0d/0x1b25 cmp word [0x4578],0`).

--------------------------------------------------------------------------------
## 9. Road completion

Trigger (§5.5) at z >= (rows-0.5)<<16. fn_0533(z32, x, y) (0x533) → 1 iff:
- tile (z,x) has block type ∈ {0x100, 0x300, 0x500} — a tunnel form
  (`0x548..0x567`), and
- (y-0x2200)/0x80 < (outer[t]+inner[t])/2 (`0x5ad..0x5d9`, t = distance from
  column center as in fn_1685) — ship flies low enough to be inside the
  tunnel mouth (jumping over the gate does not finish).

Ground truth: the last row of every retail road contains tunnel-type tiles at
the reachable column(s) (e.g. road00 `0x0106` center; road02 full row of
`0x010e`; road28 `0x0301`; road30 `0x0305`) — the finish line is literally a
tunnel gate on the final row.

fn_0e58() (0xe58) — completion fly-away: ship_y=0 (`0xe5e`), [0x160c]=0; for
72 ticks (`0xea1 cmp word [0x160c],0x48`): ESC → return 7; render fn_0be3(0),
wait tick fn_0e3e, flip fn_0e23, z32 += speed32 (`0xe92..0xe9d`); then
**return 0** (`0xeab mov ax,0x0`). So "completed" = 0 after a 2 s cruise.

--------------------------------------------------------------------------------
## 10. Input — fn_074c (0x74c)

Dispatch on [0x9602] (`0xad3`): 0 keyboard, 1 joystick, 2 mouse, 3 demo.

Keyboard (`0x758..0x903`), cells ds:0xba2+ (bit 7 = held):
- steer_0x9600 = (right 0xba5 | pgup 0xba7 | pgdn 0xba9) - (left 0xba4 |
  home 0xba6 | end 0xba8) ∈ {-1,0,1} (`0x80d..0x81c`).
- accel_0x933c = (up 0xba2 | home | pgup) - (down 0xba3 | end | pgdn)
  (`0x8d4..0x8e3`).
- jumpkey_0x5488 = space 0xbab (`0x8e6..0x900`).

Joystick (`0x9a0..0xa46`): axes fn_05f6(1)/fn_05f6(2) vs. calibration centers
[0x5180]/[0x548a] (captured by fn_0707 at road start): left if x<cx/2, right
if x>3*cx/2, accel if y<cy/2, brake if y>3*cy/2; jump = button fn_0672.

Mouse (`0x906..0x99d`): x<0x96 left / x>0xaa right; y>0xb9 brake / y<0xf
accel; jump = fn_06b9(2) buttons; x recentered to 0xa0 each frame.

**Demo (`0xa49..0xacd`)**: one byte per 0x666 (1638) z-units:
`idx = z32 / 0x666` (`0xa49 mov ax,0x666 ... call 0x5d4e`), byte =
`[idx + 0x962e]` (encoded `mov al,[bx-0x69d2]`; 0x10000-0x69d2 = 0x962e =
DEMO.REC buffer, 0x18fe bytes).
- accel = (byte & 3) - 1 (`0xa69 and ax,3; 0xa6c add ax,0xffff`)
- steer = ((byte >> 2) & 3) - 1 (`0xa8f shr al,1 ×2`)
- jump  = (byte >> 4) & 1 (`0xabc shr ×4; and ax,1`)
Indexing by position (≈40 samples/row) makes playback speed-independent.
6398 bytes cover ≈160 rows = the demo road's exact length (road00: 160 rows).

--------------------------------------------------------------------------------
## 11. Rendering hooks (gameplay-visible parts of fn_0be3, 0xbe3)

- in_tunnel = fn_0533(ship pos) (`0xbf9`).
- Explosion: sprite = expl_ctr/3 (`0xc0c cx=3 div`) → frames 0..13, nothing
  (si=-1) at 14 (`0xc18 cmp si,0xe`).
- Ship sprite index = (column*3 + pitch)*3 + 0xe + flame (`0xc7c..0xc93`):
  - column = fn_0b34(x) = clamp((x/0x80 - 95)/46, 0..6) (0xb34) — perspective
    variant per lane.
  - pitch = 0 in tunnels; else fn_0afa(yvel, y) (0xafa): 2 if yvel <= -0x163
    (-355) or y < 0x2800 (nose down), 1 if yvel >= 0x163 (nose up), else 0.
  - flame = table ds:0xea = {0,1,2,1} indexed by ([0x160c]/2)&3 (`0xc36..0xc4b`),
    forced 0 when end_state==4 (out of fuel, `0xc26..0xc33`).
  - sprite byte offset = index*0x2d0 (720 = 24x30) into CARS strip [0xaf44].
    14 explosion + 63 ship sprites = 77*720 = 55440 = full 24x2310 strip. ✓
- Sticky: ship drawn at y-0x80 (sunk; `0xd25..0xd3a`, flag arg [bp+4]).
- Shadow: support heights fn_0b71 at x±0x380 (`0xc98 add ax,0xfc80` /
  `0xcb6 add ax,0x380`), shadow dy = (ship_y - min)/0x80 (`0xd81..0xda2`);
  fn_0b71 (0xb71) returns block-top from ds:0xde, except tunnels (type 1) and
  in_tunnel mode → ground; 0 if void.
- Skip-redraw cache 0xe28..0xe32 (z,x,y,sprite,page) `0xccb..0xd0e`.
- fn_2d03 (0x2d03) stores 8 params to 0xe34..0xe42 and jumps through the
  render function pointer [0xe44] (viewport renderer, out of scope here).

Dashboard (fn_124b, 0x124b) — all at 36 Hz, drawing only deltas:
- speedometer = (speed32 - ap_delta)/0x141 capped 0x22 (34 segments)
  (`0x127d cx=0x141; 0x128d cmp ...,0x22`); shapes via SPEED.DAT table
  ds:0x4580 + base ptr [0x54b0/0x54b2]; cache [0x41ca]. Max speed 0x2aaa/0x141
  = 34 → full scale. Wall-kill threshold 0xe38/0x141 ≈ 11.3 segments.
- oxygen segments = (oxy + 0xbb7)/0xbb8 capped 10 (`0x130d/0x131a`), shapes
  OXY_DISP via table ds:0x9604 (`mov ax,[bx-0x69fc]`) + ptr [0x5482/84],
  cache [0x457a]; fuel likewise (`0x13f3..0x147d`), table ds:0x548c + ptr
  [0x961a/1c], cache [0x9618].
- empty-tank warning: when end_state==5 (oxy): color-swap blink of 7x7 cell at
  (0xa0,0xa1) every 9-tick phase (fn_11d5 swaps colors 0x63/0x64 VGA, 0xf/4
  EGA) + **sfx 3** on each on-phase (`0x13eb push 3; call 0x3c2`); end_state==4
  (fuel): 16x5 cell at (0x9b,0xa9) (`0x14bc..0x14d6`).
- progress bar: ticks = (z32 - 3<<16) / ((rows-3)<<16 / 0x1e) capped 0x1d
  (`0x14dc..0x1527`; 30 ticks, start row 3); drawn at x = 0x2a+i, scan-fill
  column up from y=0x8f (fn_116b).
- autopilot light: when ap_light_0x4568 changes: fn_0fc6 26x5 cells at
  (0xcb,0x9c) from ds:0x204 + 0x82*state (`0x1559..0x1573`).
- gravity number (play_road §2): fn_1067(x,y,value,ndigits) renders 4x5 digit
  shapes ds:0x13c+digit*0x14, divisors ds:0x308 {1000,100,10,1}.

Sound effects (fn_03c2(n), 0x3c2): n=0 explosion, 1 bounce thud, 2 bump/
scrape, 3 warning beep, 4 supplies refill. SB path: SFX.SND segment [0x456c],
offset table inside; PC speaker path: stream ptr table ds:0x132 (5 entries:
0x00f2 0x010e 0x0114 0x0118 0x0124) → [0xba0] for the timer ISR. [0xaf48] =
start tick; fn_0476 (0x476) reports "sfx busy" = [0x160c] < [0xaf48]+8 on SB,
or [ [0xba0] ]!=0 on speaker.

--------------------------------------------------------------------------------
## 12. Quick constants reference

| thing | value |
|---|---|
| tick rate | 36 Hz ([0x160c], PIT ISR) |
| ship z start | row 3.0 (z32 = 0x30000) |
| ship x start / column width | 0x8000 / 0x1700 (46 px), 7 columns, x∈[0x2f80,0xd080) |
| deck top / half top / full top | y = 0x2800 / 0x3200 / 0x3c00 |
| deck slab solid range | 0x1e80 < y < 0x2800 (free below 0x2180) |
| ship half-width | 0x700 (14 px) |
| max speed / accel / boost / sticky / scrape | 0x2aaa / ±0x4b / +0x12f / -0x12f / -0x97 per tick |
| wall-kill speed | >= 0xe38 (else stop + bump) |
| jump velocity / gravity accel | 0x480 up; -(g*0x1680/0x190) = -14.4g per tick |
| jump allowed | gravity word < 0x14 (20) |
| air-steer window | first 0xf00 of height gain, only if xvel==0 |
| bounce | if |yvel| >= 0x104*g/8: yvel = -yvel/2, else 0 |
| void fall | below y=0x2800: yvel forced <= -0x6a; death code 3 |
| edge slide | probes ±i*0x80 (i=1..14) at y-1; side_push ±0x11/tick² |
| corner nudge | ±0x3a0 sideways when clipping a wall edge |
| fuel / oxygen tank | 30000 (0x7530) each; refill to full on supplies tile |
| fuel burn / O2 burn | 30000/word2 per row; 30000/(36*word3) per tick |
| death display time | 108 ticks (3 s); explosion anim 42 ticks (14 frames @3) |
| completion cruise | 72 ticks (2 s) then return 0 |
| autopilot | always on ([0x457e]=1); fires once per jump at y>=0x3700; tries ±10..60% xvel, then ±10..60% speed; speed delta reverted on landing |
| return codes | 0 done, 1 crash, 2 burned, 3 fell, 4 fuel, 5 oxygen, 7 ESC |

## 13. Uncertain / TBD

- [0x41cc] "controls dead" gate and its 144-tick → code 2 path appear
  unreachable (no writer found in the image; BSS-zeroed). Marked dead code.
- [0x4570] (param to renderer fn_2d03 via fn_0be3) is only ever 0; purpose in
  the renderer unknown (likely "force full redraw").
- [0x517c] zeroed at fn_1f2c entry (0x1f47); no reader found — dead.
- fn_0533's profile test for raised tunnel gates (types 0x300/0x500) uses the
  same absolute y tables as ground tunnels; how that plays with road30's
  0x0305 finish (full-height bore) was not traced further.
- Exact visual meaning of surface nibble values other than 2/8/9/a/c (plain
  colors) is renderer territory (trekdat tile textures), not decoded here.
- 0x2f80..0xd080 guards in fn_1f2c §5.12 only prevent unsigned wrap; real
  lateral bounds come from fn_04c0 returning 0 (void) outside the road.

--------------------------------------------------------------------------------
## ERRATA (verified against raw asm + DOSBox telemetry during porting)

1. §5.12: the +0x618 lateral term applies when NOT on sticky (0x2634 jnz);
   sticky REMOVES the bonus. (The note had it inverted.)
2. §5.17: "ap_delta/40" means the 32-bit pair [0xaf3e]/[0xaf40]; the revert
   is a full 32-bit subtract.
3. §7 fn_1bb5: a zero SURFACE nibble is BAD only when there is NO block
   (type==0). Zero-surface block tops (e.g. 0x0400) are LANDABLE (0x1bfd).
   This decides autopilot branches on block-heavy roads.
4. §5.15: wall-hit zeroes speed on BOTH paths (explosion too, 0x27e1→0x2814);
   the explosion fires when expl_ctr==0 even if end_state!=0 (only the
   end_state=1 assignment is conditional).
5. §8: supplies gated on end_state==0; sticky gated on expl_ctr==0 (0x1ad6,
   0x1b0d).
6. §4/§5.20: the three death causes overwrite each other (last wins);
   end_frames does not count the tick that sets end_state (0x2a8b..0x2ac9).
7. fn_1584 profile row index is UNSIGNED: (y+0xDE00)/0x80 — y<0x2200 wraps
   to ~511 (not solid in tunnels, solid for bores). Same in fn_0533.

With these corrections the reimplementation plays DEMO.REC to completion
(1775 ticks, 49.3s) matching instrumented-original telemetry.
