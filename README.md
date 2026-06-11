# SkyRoads — a 1:1 reverse-engineered portable port

In 1993, Bluemoon Interactive — the Estonian studio whose founders later
built the technology behind Kazaa and Skype — released **SkyRoads**, a
gorgeous space-racing game for DOS. The source code was never published.

This repo is a from-scratch reverse engineering of that game: the
original `SKYROADS.EXE` was disassembled, every file format and algorithm
was recovered from the machine code, and the whole engine was rewritten
as ~3,000 lines of portable C11. It runs today on **macOS, Windows,
Linux, the web (Emscripten), and iOS** — with the exact 36.0036 Hz timing,
fixed-point physics, and pre-baked-perspective renderer of the original.

Made as a love letter to Bluemoon Interactive and everyone who spent the
90s bunny-hopping down neon roads in space. All game content is theirs;
SkyRoads was released as freeware, and the readme that ships with it
explicitly welcomes modification and reverse engineering.

## How faithful is it?

This is not a remake "in the spirit of" the original — it aims to be the
same game:

- **No original code executes.** The engine is new C, linking only SDL2.
- **Demo-proven physics.** The original's 1993 attract-demo input
  recording (`DEMO.REC`) is position-indexed raw input. Replayed through
  this engine's reimplemented physics, it completes the entire demo road
  tick-for-tick — jump arcs, boost pads, sticky tiles, collisions, and
  the hidden landing autopilot all have to be exact for that to work.
  It's a regression test: `ctest --test-dir build`.
- **Receipts included.** `docs/FORMATS.md` specifies every file format,
  and `re/notes/` documents the recovered game logic with the EXE
  instruction address for every constant.
- **Original data, untouched.** The engine reads the unmodified retail
  files at runtime. No converted assets.

## Getting the game data

**This repository contains no Bluemoon files.** SkyRoads is freeware —
download it from [bluemoon.ee](https://www.bluemoon.ee/history/skyroads/)
and copy the data files (`*.LZS`, `*.DAT`, `*.SND`, `DEMO.REC`) into the
repo root (they're gitignored), or pass their folder to the executable.

## Build & run

```sh
cmake -B build -S . && cmake --build build
./build/skyroads /path/to/skyroads-data     # or run from the data dir
```

Web, iOS (including TestFlight), and Windows cross-builds:
see [docs/PORTING.md](docs/PORTING.md).

Controls: arrows steer/throttle, Space jumps, Esc back, P pause — same as
DOS. Touch (iOS): d-pad lower-left, jump button lower-right.

## Make your own tracks

Because the formats are fully decoded, the game is moddable:

```sh
python3 tools/make_track.py /path/to/gamedir /tmp/mod
```

builds a brand-new road (custom layout, palette, gravity/fuel/oxygen) and
splices it into a copy of `ROADS.LZS`. Tile semantics are in
`docs/FORMATS.md` §3 — geometry, tunnels, boost/sticky/ice/burning
surfaces, fuel pickups, per-road palettes are all yours to edit.

## Layout

- `src/core/` — platform-independent engine core (C11, no OS calls)
- `src/platform/` — SDL2 shell (desktop/web/iOS), iOS bundle bits
- `src/tools/` — headless render/demo/music test harnesses
- `tools/` — Python: format extractor, LZS codec, track builder
- `docs/` — format specs and porting guide
- `re/` — function map and per-subsystem reverse-engineering notes

## Credits

- **Bluemoon Interactive** — SkyRoads (1993): game design, art, music,
  and three decades of inspiration. Copyright remains theirs.
- Port by **Ammaar Reshi** and **Claude (Fable 5)**, reverse engineered
  from the retail binary in roughly a day.
- [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) (LGPL-2.1) for
  Yamaha OPL2 chip emulation; `font8x8` (public domain) for two strings
  of UI text.

## License

The port's source code is MIT (see [LICENSE](LICENSE)), except
`src/thirdparty/` which keeps its upstream licenses. SkyRoads game
content is not covered by this license and is not distributed here.
