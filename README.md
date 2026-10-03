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

**Linux**

```sh
# Debian/Ubuntu: sudo apt install cmake g++ ninja-build libsdl3-dev
# (without libsdl3-dev CMake builds SDL3 from source: install the X11 /
#  Wayland / ALSA / PulseAudio -dev packages listed in .github/workflows/release.yml)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The Linux release binary is built by GitHub Actions
([release.yml](.github/workflows/release.yml)) on Ubuntu 22.04 with SDL3
linked in and attached to each release as `SealTeam-<version>-linux-x64.tar.gz`;
every push to `main` also runs that build as a check.

**macOS**

```sh
brew install cmake sdl3            # sdl3 optional
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                # -> build/SealTeam.app
```

The release build is one universal app (Apple Silicon and Intel, macOS 11+)
built the same way with
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON`
(SDL3 then comes from source for both architectures). It is built by the same
workflow and attached as `SealTeam-<version>-macos-universal.zip`. It is
ad-hoc signed only, not notarized, so the first launch needs *Open Anyway* in
System Settings > Privacy & Security.

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
the current directory. File names may be upper or lower case (Linux). On
Linux run `./sealteam`; settings and screenshots are written next to the
binary.

### Original and Enhanced

When the port starts it shows a start menu:

* **Original Game** plays 1:1 like the DOS version: 320x200, the original
  view distance, timings and rules, including the original's quirks.
* **Enhanced Game** keeps the same game but renders the 3D view at the
  window's own resolution (any size up to 4K and beyond, or a fixed multiple
  of 320x200 for weaker machines), fills a wide screen with a wider field of
  view (or keeps 4:3), can let the mission view fill the whole screen with
  the HUD drawn over it (Full-screen 3D), draws much farther, up to the
  whole world, and shows what a shot hit (Impact effects: blood, sparks,
  splashes, wood and stone chips, leaves, with a small particle burst); the
  2D screens, HUD and fonts stay pixel-art on top. It can also play with a
  few changed rules (Modern gameplay, below).
* **Setup** changes window size, fullscreen, 4:3 aspect, smooth scaling, the
  Enhanced options (3D resolution, wide view, full-screen 3D, draw distance,
  impact effects, modern gameplay) and sound (AdLib or Sound Blaster Pro 2
  music, digital or FM effects, volumes).
* **Controller** shows the game controller layout (Xbox-style default: left
  stick moves and turns, right stick is the camera, A fires / selects, B
  cancels, LB / RB hold the view and order layers) and remaps every action;
  see `docs/controller.md`.

**Modern gameplay** (Setup; Enhanced game only, off by default) is the one
option that changes gameplay rules rather than the presentation: more
realistic loadouts and fixes of the original's rougher edges. The rules so
far:

* The CAR-15 Commando carries as much ammunition as the M16: 12 magazines of
  20 rounds (240) instead of 8 (160). The extra magazines weigh their share.
* The M79 grenadier carries a full vest of 40 mm rounds: 20 instead of 5.
* Support craft hold their fire near friendlies: an attacking boat,
  helicopter or aircraft no longer rolls its bursts against the Point Man
  (the original aims every "area" shot at him, wherever he is) and does not
  fire while a SEAL stands near the target or the line of fire.
* Squad mates work around obstacles: a SEAL who keeps bouncing off the same
  tree or hut walks around it instead of being steered back into it.
* Enemy grenade discipline: enemy soldiers throw grenades only at close
  range and at most one every four seconds, and a rifleman who runs dry no
  longer turns into a grenade machine.
* The snatch target is marked: the official, courier or tax collector a
  Snatch mission has you capture wears a small red scarf at his neck,
  his mark on the map screen gets a light red ring and a "!" once the
  map shows him at all, and the HUD names him "(target)" when he is your
  target. Nothing else about him changes.
* No bushes inside buildings: the original checks for a building at the
  corner of a ground-cover cell and then shifts the plant by up to 128
  units, so plants grow inside huts and show through the walls; the final
  spot is checked too (such a plant also gave cover to anyone inside).
* Callable F-4 Phantom air strikes: a flight of two Phantoms (the jets the
  original only shows as fly-overs) waits off-map behind the insertion
  point. On the map, `g` (or the flight's attack button) sends it over the
  support waypoint, where each aircraft drops one bomb with the game's
  biggest blast; three strikes per mission, then "Phantoms are Winchester."
  The flight holds its bombs when a SEAL is close to the impact point.

The briefing's loadout screen shows and limits the magazines accordingly;
`docs/mission.md` ("Modern gameplay") has the details.

Settings are saved in `sealteam.cfg` next to the program (macOS: in
`~/Library/Application Support/SealTeam/`). `--original` or
`--enhanced` skip the menu; the menu can be switched off in Setup and
brought back with `--launcher`.

Keys the port adds on every screen: **Ctrl+H** shows a reference card of
the game's keys (any key closes it; the mission clock stops meanwhile),
**Ctrl+Q** quits to the desktop at once, **Alt+Enter** toggles fullscreen,
**F12** saves a screenshot next to the program. The **mouse wheel** zooms
the map screen and the chase / team cameras, and a tap of the **middle
mouse button** swings the chase / team camera back behind the soldier it
follows (after panning it around with the right button).

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

## Credits

* **PlasticCog** — project lead, reverse engineering, port
* **Witchiewoman** — game testing (the bug reports and gameplay feedback
  behind many of the fixes and the Modern Gameplay option)

## Legal

SEAL Team is © 1993 Andre Gagnon and Electronic Arts. This project is an
independent, non-commercial reimplementation and is not affiliated with or
endorsed by the rights holders. It contains no original game code or assets.

The port's own code is released under the MIT License (see `LICENSE`).
Third-party components and their licenses are listed in `THIRD_PARTY.md`.
