# SEAL Team — source port

A reimplementation of the 1993 DOS game **SEAL Team** (Andre Gagnon /
Electronic Arts) in modern C++17 with SDL3, based on reverse engineering of the
original `st.exe`. The goal is an accurate port: the same rules, timings,
screens and data, running natively on current systems.

> **You need your own copy of the original game.** This repository contains no
> game data. Copy all files of your SEAL Team installation (`st.exe`, `*.lib`,
> `*.fnt`, the sound drivers, ...) into a folder named **`Game`**. At runtime
> the port reads everything, including text and tables stored inside `st.exe`,
> from that folder.

## Status

The whole game is ported and playable: campaign, practice and demo modes from
the title screen through briefing, missions, debriefing and awards, in the
Original (1:1) and Enhanced presets. Every module was written from a full
reverse engineering of the original executable and checked against it; the
renderer and the map screen were compared pixel by pixel with the original
running in DOSBox. Still rough: some HUD details were checked visually only, and the port has had little playtesting yet, so
expect bugs (see `docs/loop.md`, `docs/mission.md` and `docs/front.md` for the
known deviations).

- [x] EALIB archive reader and LZSS decompression (byte-exact against all 555 compressed entries)
- [x] PXPK pictures and VGA palettes
- [x] Mode X VGA emulation (planar memory as linear pixels, page flipping), SDL3 window, 4:3 aspect
- [x] Keyboard (PC scancodes + BIOS key queue), mouse, game controller (SDL3 gamepad, remappable), PIT/retrace timing
- [x] Title screen through the full data pipeline
- [x] Symbol map of the whole executable: 1354 functions and 1008 globals named, notes per module in `docs/re/`
- [x] Graphics library: Mode X pages, clipping, dithered primitives, polygons, lines, scaled RLE sprites, fonts, masks
- [x] 256 Hz game timer, frame pacing, palette fades, screen shake
- [x] Front end: main menu, campaign, recruits, briefing, debriefing
- [x] 3D world and renderer (Original and Enhanced presets)
- [x] Mission simulation: world, units, AI, combat, craft, objectives
- [x] Mission loop: cameras and views, HUD, map screen / control panel, time compression
- [x] Sound: XMIDI music and VOC effects

## Building

Requirements: CMake 3.16+, a C++17 compiler and git. SDL3 is used if it is
installed; otherwise CMake downloads and builds SDL 3.2 automatically (the
first configure then takes a few minutes).

**Windows (MSYS2 UCRT64)**

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja}
pacman -S mingw-w64-ucrt-x86_64-sdl3          # optional, skips building SDL3
cmake -S . -B build -G Ninja                   # or -G "MinGW Makefiles" with mingw32-make
cmake --build build
```

**Linux / macOS**

```sh
# Debian/Ubuntu: sudo apt install cmake g++ libsdl3-dev   (libsdl3-dev optional)
# macOS:         brew install cmake sdl3                  (sdl3 optional)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Visual Studio**: open the folder as a CMake project, or install SDL3 via
vcpkg (`vcpkg install sdl3`) and configure with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.

## Running

Put the original game files in a folder named `Game`:

```
sealteam.exe
Game/
    st.exe
    main.lib
    ...
```

The `Game` folder is looked up next to the executable, one or two directories
above it (so `build/sealteam.exe` finds `Game/` in the repository root), and in
the current directory.

### Original and Enhanced

When the port starts it shows a start menu:

* **Original Game** plays 1:1 like the DOS version: 320x200, the original
  view distance, timings and rules, including the original's quirks.
* **Enhanced Game** keeps the same game but renders the 3D view at the
  window's own resolution (any size up to 4K and beyond, or a fixed multiple
  of 320x200 for weaker machines), fills a wide screen with a wider field of
  view (or keeps 4:3), can let the mission view fill the whole screen with
  the HUD drawn over it (Full-screen 3D), and draws much farther, up to the
  whole world; the 2D screens, HUD and fonts stay pixel-art on top.
* **Setup** changes window size, fullscreen, 4:3 aspect, smooth scaling, the
  Enhanced options (3D resolution, wide view, full-screen 3D, draw distance) and sound (AdLib
  or Sound Blaster Pro 2 music, digital or FM effects, volumes).
* **Controller** shows the game controller layout (Xbox-style default: left
  stick moves and turns, right stick is the camera, A fires / selects, B
  cancels, LB / RB hold the view and order layers) and remaps every action;
  see `docs/controller.md`.

Settings are saved in `sealteam.cfg` next to the program. `--original` or
`--enhanced` skip the menu; the menu can be switched off in Setup and
brought back with `--launcher`.

| Option | Effect |
|--------|--------|
| `--original` / `--enhanced` | start directly with that preset |
| `--launcher` | show the start menu even if it was switched off |
| `--data DIR` | use another directory instead of `Game` |
| `--scale N` | window scale (default from Setup) |
| `--window WxH` | window size in pixels instead of the scale |
| `--fullscreen` | start fullscreen (Alt+Enter toggles) |
| `--no-aspect` | square pixels instead of 4:3 |
| `--shot FILE --shot-after S` | save a screenshot after S seconds and quit (testing) |

Original options are passed through: `?`/`h` help, `d` digital sound off, `t`
skip title screen, and a mission number.

## Repository layout

| Path | Contents |
|------|----------|
| `src/core` | shared types, logging |
| `src/platform` | SDL3 backend: VGA emulation, input, timer |
| `src/data` | game file access, EALIB/LZSS, `st.exe` image reader |
| `src/gfx` | pictures, palettes, drawing |
| `src/game` | the ported game modules |
| `docs/formats.md` | data file formats |
| `docs/controller.md` | game controller layout, remapping, how the mapping layer works |
| `docs/re/` | reverse-engineering notes per original module |
| `tools/` | Python and Ghidra scripts used for the reverse engineering |

## Reverse-engineering workflow

The original game files and Ghidra are kept out of the repository (see
`.gitignore`). To reproduce the analysis, put the game in `Game/` and
Ghidra 12.x in `ghidra_<version>/`, then run

```sh
analyzeHeadless re/ghidra_proj SealTeam -import Game/st.exe \
    -scriptPath tools/ghidra -preScript SetDGroup.java 56bf \
    -postScript ApplySymbols.java tools/re/symbols \
    -postScript ExportAll.java re/export
python tools/ealib.py Game/*.lib -x re/assets
```

Conventions for addresses, names and notes are in `docs/re/CONVENTIONS.md`.

## Legal

SEAL Team is © 1993 Andre Gagnon and Electronic Arts. This project is an
independent, non-commercial reimplementation and is not affiliated with or
endorsed by the rights holders. It contains no original game code or assets.

The port's own code is released under the MIT License (see `LICENSE`).
Third-party components and their licenses are listed in `THIRD_PARTY.md`.
