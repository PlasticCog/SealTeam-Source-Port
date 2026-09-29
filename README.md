# SEAL Team — source port

A reimplementation of the 1993 DOS game **SEAL Team** (Andre Gagnon /
Electronic Arts) in modern C++17 with SDL2, based on reverse engineering of the
original `st.exe`. The goal is an accurate port: the same rules, timings,
screens and data, running natively on current systems.

> **You need your own copy of the original game.** This repository contains no
> game data. At runtime the port reads everything, including text and tables
> stored inside `st.exe`, from your installation.

## Status

Early work in progress.

- [x] EALIB archive reader and LZSS decompression (byte-exact against all 555 compressed entries)
- [x] PXPK pictures and VGA palettes
- [x] Mode X VGA emulation (planar memory as linear pixels, page flipping), SDL2 window, 4:3 aspect
- [x] Keyboard (PC scancodes + BIOS key queue), mouse, PIT/retrace timing
- [x] Title screen through the full data pipeline
- [ ] Symbol map of the whole executable (in progress, `docs/re/`)
- [ ] Graphics library primitives, fonts, sprites (RLE/RLX)
- [ ] Front end: main menu, campaign, recruits, briefing, debriefing
- [ ] Mission engine: world, units, AI, combat
- [ ] Sound: XMIDI music and VOC effects

## Building

Requirements: CMake 3.16+, a C++17 compiler and SDL2.

**Windows (MSYS2 UCRT64)**

```sh
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,SDL2}
cmake -S . -B build -G Ninja
cmake --build build
```

**Linux / macOS**

```sh
# Debian/Ubuntu: sudo apt install cmake g++ libsdl2-dev
# macOS:         brew install cmake sdl2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Visual Studio**: install SDL2 via vcpkg (`vcpkg install sdl2`) and configure
with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.

## Running

```sh
sealteam --data /path/to/SealTeam        # directory containing st.exe and *.lib
```

Without `--data` the port looks in `./SealTeam-DOS`, `./Game` and the current
directory.

| Option | Effect |
|--------|--------|
| `--data DIR` | location of the original game files |
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
| `src/platform` | SDL2 backend: VGA emulation, input, timer |
| `src/data` | game file access, EALIB/LZSS, `st.exe` image reader |
| `src/gfx` | pictures, palettes, drawing |
| `src/game` | the ported game modules |
| `docs/formats.md` | data file formats |
| `docs/re/` | reverse-engineering notes per original module |
| `tools/` | Python and Ghidra scripts used for the reverse engineering |

## Reverse-engineering workflow

The original game files and Ghidra are kept out of the repository (see
`.gitignore`). To reproduce the analysis, put the game in `SealTeam-DOS/` and
Ghidra 12.x in `ghidra_<version>/`, then run

```sh
analyzeHeadless re/ghidra_proj SealTeam -import SealTeam-DOS/st.exe \
    -scriptPath tools/ghidra -preScript SetDGroup.java 56bf \
    -postScript ApplySymbols.java tools/re/symbols \
    -postScript ExportAll.java re/export
python tools/ealib.py SealTeam-DOS/*.lib -x re/assets
```

Conventions for addresses, names and notes are in `docs/re/CONVENTIONS.md`.

## Legal

SEAL Team is © 1993 Andre Gagnon and Electronic Arts. This project is an
independent, non-commercial reimplementation and is not affiliated with or
endorsed by the rights holders. It contains no original game code or assets.
