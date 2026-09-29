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

Early work in progress.

- [x] EALIB archive reader and LZSS decompression (byte-exact against all 555 compressed entries)
- [x] PXPK pictures and VGA palettes
- [x] Mode X VGA emulation (planar memory as linear pixels, page flipping), SDL3 window, 4:3 aspect
- [x] Keyboard (PC scancodes + BIOS key queue), mouse, PIT/retrace timing
- [x] Title screen through the full data pipeline
- [ ] Symbol map of the whole executable (in progress, `docs/re/`)
- [ ] Graphics library primitives, fonts, sprites (RLE/RLX)
- [ ] Front end: main menu, campaign, recruits, briefing, debriefing
- [ ] Mission engine: world, units, AI, combat
- [ ] Sound: XMIDI music and VOC effects

## Building

Requirements: CMake 3.16+, a C++17 compiler and git. SDL3 is used if it is
installed; otherwise CMake downloads and builds SDL 3.2 automatically (the
first configure then takes a few minutes).

**Windows (MSYS2 UCRT64)**

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja}
pacman -S mingw-w64-ucrt-x86_64-sdl3          # optional, skips building SDL3
cmake -S . -B build -G Ninja
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

| Option | Effect |
|--------|--------|
| `--data DIR` | use another directory instead of `Game` |
| `--scale N` | window scale (default 3) |
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
