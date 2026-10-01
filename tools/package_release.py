#!/usr/bin/env python3
"""Package a release: the game binary, LICENSE, the player README and an
empty "Game" folder (with a note) where the original files go.

    python tools/package_release.py <version> <path/to/binary> [--platform windows|linux] [--out DIR]

Windows: <out>/SealTeam-<version>-windows-x64.zip  (sealteam.exe)
Linux:   <out>/SealTeam-<version>-linux-x64.tar.gz (sealteam, executable)
Default out: re/release. The Linux package is normally built by the GitHub
Actions workflow (.github/workflows/release.yml).
"""
import argparse
import os
import shutil
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))

GAME_NOTE = """Copy ALL files from your original SEAL Team installation into this folder:

  st.exe, setd.exe, setm.exe
  *.lib      (main.lib, main2.lib, ... worlds.lib, sound.lib, ...)
  *.fnt      (4x6.fnt, memo.fnt, prop.fnt, propbold.fnt)
  *.adv, *.add, *.adg, *.adi   (the sound drivers)
  *.cmp, s.cnf, st1.dfr        (campaign saves and settings)

Then run the program from the folder above. The port reads everything,
including text and tables stored inside st.exe, from these files; nothing
is copied into the port itself. Upper or lower case file names both work.
This file can stay here; it is ignored.
"""

README = """SEAL Team source port {version} ({platform_name})
===========================

A reimplementation of the 1993 DOS game SEAL Team in modern C++ with SDL3.
Project page: https://github.com/PlasticCog/SealTeam-Source-Port

You need your own copy of the original game. This package contains no game
data.

Setup
-----
1. Copy ALL files of your SEAL Team installation into the "Game" folder
   next to {binary} (see the note inside that folder).
2. {run}

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
Ctrl+H shows a reference card of the game's keys (any key closes it),
Ctrl+Q quits to the desktop at once, Alt+Enter toggles fullscreen. The
mouse is captured by the game window; Alt+Tab releases it and the next
click takes it back.

Command line: {binary} [--original | --enhanced | --launcher] [--data DIR]
Original options still work, e.g. "{binary} 3 t" starts mission 3 without
the title screen.

Press F12 in the game to save a screenshot next to the program; a crash
writes sealteam-crash.txt there. The port has had little playtesting;
please report problems on the project page.
{platform_notes}
License: the port's code is MIT licensed (see LICENSE.txt). It uses
ymfm (BSD-3-Clause) and SDL3 (zlib).
"""

PLATFORMS = {
    'windows': dict(
        platform_name='Windows x64', binary='sealteam.exe', suffix='windows-x64', archive='zip',
        run='Run sealteam.exe.',
        notes=''),
    'linux': dict(
        platform_name='Linux x86-64', binary='sealteam', suffix='linux-x64', archive='gztar',
        run='Run ./sealteam (from a terminal or your file manager).',
        notes="""
Linux notes
-----------
The binary is built on Ubuntu 22.04 and needs only glibc 2.35 or newer;
SDL3 is linked in and loads X11 / Wayland and ALSA / PulseAudio / PipeWire
from your system at run time. If the file manager refuses to run it, mark
it executable: chmod +x sealteam. Settings and screenshots are written
next to the binary, so keep it in a folder you can write to.
"""),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('version')
    ap.add_argument('binary', help='the built sealteam executable')
    ap.add_argument('--platform', choices=sorted(PLATFORMS), default='windows')
    ap.add_argument('--out', default=os.path.join(ROOT, 're', 'release'))
    args = ap.parse_args()
    p = PLATFORMS[args.platform]
    name = f'SealTeam-{args.version}'
    stage = os.path.join(args.out, name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, 'Game'))
    binary = os.path.join(stage, p['binary'])
    shutil.copy(args.binary, binary)
    os.chmod(binary, 0o755)
    shutil.copy(os.path.join(ROOT, 'LICENSE'), os.path.join(stage, 'LICENSE.txt'))
    with open(os.path.join(stage, 'README.txt'), 'w', newline='\n') as f:
        f.write(README.format(version=args.version, platform_name=p['platform_name'], binary=p['binary'],
                              run=p['run'], platform_notes=p['notes']))
    with open(os.path.join(stage, 'Game', 'PUT GAME FILES HERE.txt'), 'w', newline='\n') as f:
        f.write(GAME_NOTE)
    base = os.path.join(args.out, f'{name}-{p["suffix"]}')
    ext = '.zip' if p['archive'] == 'zip' else '.tar.gz'
    if os.path.exists(base + ext):
        os.remove(base + ext)
    if p['archive'] == 'zip':
        shutil.make_archive(base, 'zip', args.out, name)
    else:
        # Explicit modes: the host may be Windows, where chmod means nothing.
        def modes(info):
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            if info.isdir():
                info.mode = 0o755
            else:
                info.mode = 0o755 if os.path.basename(info.name) == p['binary'] else 0o644
            return info
        with tarfile.open(base + ext, 'w:gz') as tar:
            tar.add(stage, arcname=name, filter=modes)
    print('wrote', base + ext)
    return 0


if __name__ == '__main__':
    sys.exit(main())
