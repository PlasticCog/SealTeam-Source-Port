#!/usr/bin/env python3
"""Package a release: the game binary, LICENSE, the player README and an
empty "Game" folder (with a note) where the original files go.

    python tools/package_release.py <version> <path/to/binary> [--platform windows|linux|macos] [--out DIR]

Windows: <out>/SealTeam-<version>-windows-x64.zip  (sealteam.exe)
Linux:   <out>/SealTeam-<version>-linux-x64.tar.gz (sealteam, executable)
macOS:   <out>/SealTeam-<version>-macos-universal.zip (SealTeam.app; pass the
         .app as <binary>; packaged on a Mac: ad-hoc signed, zipped by ditto)
Default out: re/release. The Linux and macOS packages are normally built by
the GitHub Actions workflow (.github/workflows/release.yml).
"""
import argparse
import os
import shutil
import subprocess
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
{setup}

The start menu offers:
  Original Game  - plays 1:1 like the DOS version (320x200, original rules)
  Enhanced Game  - same game, 3D view at your screen's native resolution
                   (4K, 1080p, ...) filling the window, draw distance up to
                   the whole world, impact effects (blood, sparks, splashes
                   and chips where shots hit)
  Setup          - window size, fullscreen, aspect, 3D resolution, wide
                   view, full-screen 3D (the mission view fills the whole
                   screen, HUD over it), draw distance, impact effects,
                   modern gameplay (Enhanced only: realistic SEAL loadouts,
                   e.g. 240 rounds for the CAR-15 and 20 x 40 mm for the
                   M79; support craft hold fire near friendlies; squad
                   mates walk around obstacles; enemy grenade discipline;
                   the snatch target wears a red scarf, is named on the HUD
                   and map; demolition charges explode where they lie;
                   no bushes inside buildings; F-4 Phantom
                   air strikes callable from the map with 'g', no
                   split needed; see the project README),
                   music device (AdLib / Sound Blaster Pro 2), digital or
                   FM effects, volumes
  Controller     - game controller layout (Xbox-style default) and
                   remapping; controllers are detected when plugged in

Settings are saved in sealteam.cfg {settings_where}. On every screen:
Ctrl+H shows a reference card of the game's keys (any key closes it),
Ctrl+Q quits to the desktop at once, Alt+Enter toggles fullscreen. The
mouse wheel zooms the map and the chase / team cameras (over the map's
team list it steps through the teams, like Tab); the middle button
swings the camera back behind your soldier. The mouse is
captured by the game window; Alt+Tab releases it and the next click takes
it back.

Command line: {cli} [--original | --enhanced | --launcher] [--data DIR]
Original options still work, e.g. "{cli} 3 t" starts mission 3 without
the title screen.

Press F12 in the game to save a screenshot {settings_where}; a crash
writes sealteam-crash.txt there. The port has had little playtesting;
please report problems on the project page.
{platform_notes}
License: the port's code is MIT licensed (see LICENSE.txt). It uses
ymfm (BSD-3-Clause) and SDL3 (zlib).
"""

SETUP = """1. Copy ALL files of your SEAL Team installation into the "Game" folder
   next to {binary} (see the note inside that folder).
2. {run}"""

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
    'macos': dict(
        platform_name='macOS universal', binary='SealTeam.app', cli='SealTeam.app/Contents/MacOS/SealTeam', suffix='macos-universal', archive='ditto',
        settings_where='in\n~/Library/Application Support/SealTeam',
        setup="""1. Move SealTeam.app to your Applications folder (or anywhere you like).
2. Open it once. It creates the folder
       ~/Library/Application Support/SealTeam/Game
   and opens it in Finder. Copy ALL files of your SEAL Team installation
   into that folder (st.exe, *.lib, *.fnt, the sound drivers, ...).
3. Open SealTeam.app again.
   A folder named "Game" next to SealTeam.app works too.""",
        notes="""
macOS notes
-----------
One universal app for Apple Silicon and Intel Macs, macOS 11 or newer.
SDL3 is built in. The app is not signed with an Apple Developer ID, so
the first time macOS says it cannot verify the app. Then open System
Settings > Privacy & Security, scroll down and click "Open Anyway" (on
macOS 14 and older, Control-click the app and choose Open). Or, in
Terminal:  xattr -dr com.apple.quarantine /Applications/SealTeam.app
On a Mac keyboard Alt is the Option key (Option+Return toggles
fullscreen); Cmd+Q also quits.
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
    binary = os.path.join(stage, p['binary'])
    if args.platform == 'macos':
        # The app keeps its Game folder in Application Support (see main.cpp).
        os.makedirs(stage)
        shutil.copytree(args.binary, binary, symlinks=True)
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', binary], check=True)
    else:
        os.makedirs(os.path.join(stage, 'Game'))
        shutil.copy(args.binary, binary)
        os.chmod(binary, 0o755)
        with open(os.path.join(stage, 'Game', 'PUT GAME FILES HERE.txt'), 'w', newline='\n') as f:
            f.write(GAME_NOTE)
    shutil.copy(os.path.join(ROOT, 'LICENSE'), os.path.join(stage, 'LICENSE.txt'))
    with open(os.path.join(stage, 'README.txt'), 'w', newline='\n') as f:
        setup = p.get('setup') or SETUP.format(binary=p['binary'], run=p['run'])
        f.write(README.format(version=args.version, platform_name=p['platform_name'], binary=p['binary'], cli=p.get('cli', p['binary']),
                              setup=setup, platform_notes=p['notes'],
                              settings_where=p.get('settings_where', 'next to the program')))
    base = os.path.join(args.out, f'{name}-{p["suffix"]}')
    ext = '.tar.gz' if p['archive'] == 'gztar' else '.zip'
    if os.path.exists(base + ext):
        os.remove(base + ext)
    if p['archive'] == 'ditto':
        # ditto keeps the bundle's symlinks, modes and signature intact.
        subprocess.run(['ditto', '-c', '-k', '--norsrc', '--noextattr', '--keepParent', stage, base + ext], check=True)
    elif p['archive'] == 'zip':
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
