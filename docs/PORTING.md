# Building & porting

The engine is split into a platform-clean core (`src/core/`, C11, no OS
calls, no floating point in game logic) and thin platform shells
(`src/platform/`). All game data is read from the original retail files at
runtime — the port contains no converted assets, which is what keeps it
verifiable against the DOS original.

**The game data is not part of this repository.** SkyRoads was released
as freeware by Bluemoon Interactive — download it (e.g. from
[bluemoon.ee](https://www.bluemoon.ee/history/skyroads/)) and copy the
data files (`*.LZS`, `*.DAT`, `*.SND`, `DEMO.REC`) into the repo root, or
pass their folder to the executable as the first argument. The iOS and
web builds bundle whatever data files are present at configure time.

## Desktop (macOS / Linux / Windows)

Requirements: CMake ≥ 3.16, SDL2.

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/skyroads .        # argument = directory with the original files
```

## Web (Emscripten)

The CMake build detects Emscripten and bundles the data files into the
preload package automatically:

```sh
emcmake cmake -B build-web -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
emrun build-web/skyroads.html
```

SDL2 maps to the browser via Emscripten's SDL port; the 36 Hz tick is
driven from requestAnimationFrame with the same rational accumulator used
on desktop, so game speed is identical.

## iOS (verified on iPhone 17 Pro / iOS 26.5 simulator)

The CMake project has a first-class iOS branch: SDL2 is fetched and built
statically, the data files are bundled into the .app Resources, the app is
landscape-locked, and saves go to the per-app pref dir.

```sh
cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64
xcodebuild -project build-ios/skyroads.xcodeproj -scheme skyroads \
           -configuration Release -sdk iphonesimulator build
xcrun simctl install booted build-ios/Release-iphonesimulator/skyroads.app
xcrun simctl launch booted ee.bluemoon.skyroads.port
```

### On-device builds & Xcode

Pass your team ID and CMake enables automatic signing (the simulator
config stays unsigned):

```sh
cmake -B build-iosdev -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DSR_IOS_TEAM=<YOUR_TEAM_ID>
xcodebuild -project build-iosdev/skyroads.xcodeproj -scheme skyroads \
           -configuration Release -sdk iphoneos -allowProvisioningUpdates build
xcrun devicectl device install app --device <name-or-udid> \
      build-iosdev/Release-iphoneos/skyroads.app
```

Or just `open build-iosdev/skyroads.xcodeproj`, pick your phone, hit Run —
it is a normal Xcode project. It's *generated*, though: make durable
changes in CMakeLists.txt / `src/platform/ios/` (Info.plist.in, the
`Assets.xcassets` app icon), not by editing the project in Xcode.

### TestFlight

The bundle already carries everything TestFlight needs (asset-catalog app
icon, launch screen key, `ITSAppUsesNonExemptEncryption=false`). Steps:

1. App Store Connect -> New App, bundle ID `ee.bluemoon.skyroads.port`
   (register the ID in the developer portal first if asked).
2. `open build-iosdev/skyroads.xcodeproj` -> Product > Archive ->
   Distribute App > TestFlight & App Store. (CLI equivalent:
   `xcodebuild archive` + `xcodebuild -exportArchive -exportOptionsPlist`
   with `method=app-store-connect`, then upload with Transporter.)
3. Internal testing (your team, up to 100 testers) starts immediately
   after processing; external testers require a brief App Review pass.
4. Bump `MACOSX_BUNDLE_BUNDLE_VERSION` in CMakeLists for each new upload.

Rights note: the repo ships no Bluemoon content, and sideloading or
internal testing of a freeware game is uncontroversial — but *public*
distribution (external TestFlight / App Store) ships Bluemoon's game
content inside your bundle under your account; get their blessing first.

### Touch controls
- **D-pad** (lower left): left/right steer, up = accelerate, down =
  brake; diagonals combine so you can steer while throttling.
- **Jump button** (lower right): hold to bunny-hop on every landing —
  the same semantics as holding Space on DOS.
- The same controls drive the menus (d-pad = arrows, button = select);
  top-left corner = Esc, top-right corner = pause.
- Held state is rebuilt from SDL's live finger list every frame
  (`SDL_GetTouchFinger`), with synthetic touch-mouse events disabled —
  event-based tracking wedges on iOS.

## Windows (cross-compiled from macOS/Linux)

```sh
brew install mingw-w64
curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz
tar xzf SDL2-devel-2.30.9-mingw.tar.gz
cmake -B build-win -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DSDL2_DIR=$PWD/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2
cmake --build build-win   # -> skyroads.exe + SDL2.dll
```
Run `skyroads.exe` from the game-data directory (or pass it as argv[1]).

## Timing model (important for fidelity)

The DOS original reprograms PIT channel 0 to divisor 6628 (≈180.02 Hz) and
advances the game on 2 of every 10 interrupts → 36.0036 Hz. The core
exposes this as the rational SR_TICK_NUM/SR_TICK_DEN; shells must call
`sr_game_tick` exactly that often on average (accumulator pattern in
`main_sdl.c`). Rendering decouples: present the framebuffer whenever, the
game state only changes on ticks.

## Verification strategy

- `sr_dump` renders deterministic frames headlessly for golden-image
  comparison (`src/tools/dump_frames.c`).
- DEMO.REC is an input recording: feeding it to the engine must reproduce
  the original demo run tick-for-tick (regression test once gameplay
  physics are wired).
- DOSBox-X side-by-side: run the retail EXE and the port with the same
  inputs and diff screenshots.
