"""Makes the Switch's OpenAoK folder from your own copy of the game.

Copy the CD's files into original_game_1999/ (see the main README), then run:

    python3 switch/package.py [--nro PATH] [--out DIR]

It prepares the game's data as build.sh does, and writes

  DIR/switch/openaok/OpenAoK.nro      the game (build-switch/OpenAoK.nro, or --nro)
  DIR/switch/openaok/game/            the game's data, laid out as the source tree is

DIR is build-switch/sd by default; give the SD card's own folder to write
straight to it. Files already there and unchanged are left alone, and what the
player keeps beside them (Profile, SaveGame, Scenario) is never touched.

Everything it writes holds your copy of the game: keep it to your own console.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# What the engine reads, from where the source tree keeps it.
ASSETS = ['extracted_data', 'original', 'sprites/terrain/blendmasks.png']
CD = ['SYSTEM/FONTS', 'GOODIES', 'GAME/AVI', 'GAME/SOUND', 'GAME/CAMPAIGN']

# Arial, which the CD does not carry: the computer's own, or Liberation Sans,
# which has Arial's metrics.
ARIAL = [
    ('/System/Library/Fonts/Supplemental/Arial.ttf', '/System/Library/Fonts/Supplemental/Arial Bold.ttf'),
    ('/Library/Fonts/Arial.ttf', '/Library/Fonts/Arial Bold.ttf'),
    ('C:/Windows/Fonts/arial.ttf', 'C:/Windows/Fonts/arialbd.ttf'),
    ('/usr/share/fonts/truetype/msttcorefonts/Arial.ttf', '/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf'),
    ('/usr/share/fonts/truetype/msttcorefonts/arial.ttf', '/usr/share/fonts/truetype/msttcorefonts/arialbd.ttf'),
    ('/usr/share/fonts/TTF/arial.ttf', '/usr/share/fonts/TTF/arialbd.ttf'),
    ('/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf',
     '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf'),
    ('/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf',
     '/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf'),
]

copied = 0
kept = 0
size = 0


def copy(src, dst):
    """Copies one file unless an identical one is there; the data only, so a
    card formatted FAT gets no macOS metadata files."""
    global copied, kept, size
    info = os.stat(src)
    size += info.st_size
    if os.path.exists(dst):
        there = os.stat(dst)
        if there.st_size == info.st_size and int(there.st_mtime) == int(info.st_mtime):
            kept += 1
            return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    os.utime(dst, (info.st_atime, info.st_mtime))
    copied += 1


def copy_tree(src, dst):
    if os.path.isfile(src):
        copy(src, dst)
        return
    for folder, _, files in os.walk(src):
        for name in files:
            if name == '.DS_Store' or name.startswith('._'):
                continue
            path = os.path.join(folder, name)
            copy(path, os.path.join(dst, os.path.relpath(path, src)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--nro', default=os.path.join(ROOT, 'build-switch', 'OpenAoK.nro'),
                        help='the game, built by switch/build.sh')
    parser.add_argument('--out', default=os.path.join(ROOT, 'build-switch', 'sd'),
                        help='where to write switch/openaok/: the SD card, or a folder to copy to it')
    args = parser.parse_args()

    if subprocess.call([sys.executable, os.path.join(ROOT, 'tools', 'data', 'prepare_data.py')]) != 0:
        sys.exit(1)

    folder = os.path.join(args.out, 'switch', 'openaok')
    game = os.path.join(folder, 'game')
    for item in ASSETS:
        copy_tree(os.path.join(ROOT, 'assets', item), os.path.join(game, 'assets', item))
    for item in CD:
        src = os.path.join(ROOT, 'original_game_1999', item)
        if os.path.exists(src):
            copy_tree(src, os.path.join(game, 'original_game_1999', item))
    arial = next((pair for pair in ARIAL if all(os.path.exists(p) for p in pair)), None)
    if arial is not None:
        copy(arial[0], os.path.join(game, 'fonts', 'Arial.ttf'))
        copy(arial[1], os.path.join(game, 'fonts', 'Arial Bold.ttf'))
    else:
        print('warning: no Arial or Liberation Sans on this computer; the game will draw without Arial.')
    os.makedirs(os.path.join(game, 'build'), exist_ok=True)

    if os.path.exists(args.nro):
        copy(args.nro, os.path.join(folder, 'OpenAoK.nro'))
    else:
        print('note: no %s yet; build it with switch/build.sh, or put a released OpenAoK.nro beside game/.'
              % os.path.relpath(args.nro, ROOT))

    print('==> %s: %d files copied, %d already there, %d MB in all'
          % (folder, copied, kept, size // (1024 * 1024)))


if __name__ == '__main__':
    main()
