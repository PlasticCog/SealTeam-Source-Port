#!/usr/bin/env python3
"""Package a Windows release zip: the release exe, LICENSE, the player README,
and an empty "Game" folder (with a note) where the original files go.

    python tools/package_release.py <version> <path/to/sealteam.exe> [out_dir]

Produces <out_dir>/SealTeam-<version>-windows-x64.zip (default out_dir: re/release).
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))

GAME_NOTE = """Copy ALL files from your original SEAL Team installation into this folder:

  st.exe, setd.exe, setm.exe
  *.lib      (main.lib, main2.lib, ... worlds.lib, sound.lib, ...)
  *.fnt      (4x6.fnt, memo.fnt, prop.fnt, propbold.fnt)
  *.adv, *.add, *.adg, *.adi   (the sound drivers)
  *.cmp, s.cnf, st1.dfr        (campaign saves and settings)

Then run sealteam.exe from the folder above. The port reads everything,
including text and tables stored inside st.exe, from these files; nothing
is copied into the port itself. This file can stay here; it is ignored.
"""

README = """SEAL Team source port {version}
===========================

A reimplementation of the 1993 DOS game SEAL Team in modern C++ with SDL3.
Project page: https://github.com/PlasticCog/SealTeam-Source-Port

You need your own copy of the original game. This package contains no game
data.

Setup
-----
1. Copy ALL files of your SEAL Team installation into the "Game" folder
   next to sealteam.exe (see the note inside that folder).
2. Run sealteam.exe.

The start menu offers:
  Original Game  - plays 1:1 like the DOS version (320x200, original rules)
  Enhanced Game  - same game, 3D view at your screen's native resolution
                   (4K, 1080p, ...) filling the window, draw distance up to
                   the whole world
  Setup          - window size, fullscreen, aspect, 3D resolution, wide
                   view, full-screen 3D (the mission view fills the whole
                   screen, HUD over it), draw distance, music device (AdLib
                   / Sound Blaster Pro 2), digital or FM effects, volumes
  Controller     - game controller layout (Xbox-style default) and
                   remapping; controllers are detected when plugged in

Settings are saved in sealteam.cfg next to the program. On every screen:
Shift+H shows a reference card of the game's keys (any key closes it),
Ctrl+Q quits to the desktop at once, Alt+Enter toggles fullscreen. The
mouse is captured by the game window; Alt+Tab releases it and the next
click takes it back.

Command line: sealteam [--original | --enhanced | --launcher] [--data DIR]
Original options still work, e.g. "sealteam 3 t" starts mission 3 without
the title screen.

Press F12 in the game to save a screenshot next to the program; a crash
writes sealteam-crash.txt there. The port has had little playtesting;
please report problems on the project page.

License: the port's code is MIT licensed (see LICENSE.txt). It uses
ymfm (BSD-3-Clause) and SDL3 (zlib).
"""


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    version, exe = sys.argv[1], sys.argv[2]
    out_dir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, 're', 'release')
    name = f'SealTeam-{version}'
    stage = os.path.join(out_dir, name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, 'Game'))
    shutil.copy(exe, os.path.join(stage, 'sealteam.exe'))
    shutil.copy(os.path.join(ROOT, 'LICENSE'), os.path.join(stage, 'LICENSE.txt'))
    with open(os.path.join(stage, 'README.txt'), 'w') as f:
        f.write(README.format(version=version))
    with open(os.path.join(stage, 'Game', 'PUT GAME FILES HERE.txt'), 'w') as f:
        f.write(GAME_NOTE)
    zip_base = os.path.join(out_dir, f'{name}-windows-x64')
    if os.path.exists(zip_base + '.zip'):
        os.remove(zip_base + '.zip')
    shutil.make_archive(zip_base, 'zip', out_dir, name)
    print('wrote', zip_base + '.zip')
    return 0


if __name__ == '__main__':
    sys.exit(main())
