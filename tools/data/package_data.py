"""The game's data laid out for a device that plays from a folder of it: the
Switch's SD card, an Android phone.

A folder of it holds what the engine reads, in the layout of the source tree:
assets/ (prepared from the CD by prepare_data.py), the CD's fonts, movies,
music, campaigns and bonus scenario under original_game_1999/, Arial under
fonts/, an empty build/ to run in, version.txt (the data's version, which the
game checks against its own) and manifest.txt (every file's CRC-32, size and
path, which the game checks on its first start).
"""
import os
import shutil
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

sys.path.insert(0, HERE)
import prepare_data  # noqa: E402

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


class Packer:
    """Copies files into a folder, leaving alone those already there and
    unchanged, and keeps the list of what it wrote for the manifest."""

    def __init__(self):
        self.copied = 0
        self.kept = 0
        self.size = 0
        self.written = []   # (destination, source) of every file

    def copy(self, src, dst):
        """One file, unless an identical one is there. FAT keeps a file's time
        to two seconds."""
        info = os.stat(src)
        self.size += info.st_size
        self.written.append((dst, src))
        if os.path.exists(dst):
            there = os.stat(dst)
            if there.st_size == info.st_size and abs(there.st_mtime - info.st_mtime) <= 2:
                self.kept += 1
                return
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        os.utime(dst, (info.st_atime, info.st_mtime))
        self.copied += 1

    def copy_tree(self, src, dst):
        if os.path.isfile(src):
            self.copy(src, dst)
            return
        for folder, _, files in os.walk(src):
            for name in files:
                if name == '.DS_Store' or name.startswith('._'):
                    continue
                path = os.path.join(folder, name)
                self.copy(path, os.path.join(dst, os.path.relpath(path, src)))

    def lay_out(self, game):
        """The game's data into `game`, with version.txt and manifest.txt."""
        for item in ASSETS:
            self.copy_tree(os.path.join(ROOT, 'assets', item), os.path.join(game, 'assets', item))
        for item in CD:
            src = os.path.join(ROOT, 'original_game_1999', item)
            if os.path.exists(src):
                self.copy_tree(src, os.path.join(game, 'original_game_1999', item))
        arial = next((pair for pair in ARIAL if all(os.path.exists(p) for p in pair)), None)
        if arial is not None:
            self.copy(arial[0], os.path.join(game, 'fonts', 'Arial.ttf'))
            self.copy(arial[1], os.path.join(game, 'fonts', 'Arial Bold.ttf'))
        else:
            print('warning: no Arial or Liberation Sans on this computer; the game will draw without Arial.')
        os.makedirs(os.path.join(game, 'build'), exist_ok=True)
        os.makedirs(game, exist_ok=True)
        with open(os.path.join(game, 'version.txt'), 'w', encoding='utf-8', newline='\n') as fh:
            fh.write('%d\n' % prepare_data.STAMP_VERSION)
        self.write_manifest(game)

    def write_manifest(self, game):
        """One line per data file: its CRC-32, its size and its path under
        `game`."""
        lines = []
        for dst, src in self.written:
            rel = os.path.relpath(dst, game)
            if rel.startswith('..'):
                continue
            lines.append('%08x %d %s' % (checksum(src), os.path.getsize(src), rel.replace(os.sep, '/')))
        version = os.path.join(game, 'version.txt')
        lines.append('%08x %d %s' % (checksum(version), os.path.getsize(version), 'version.txt'))
        with open(os.path.join(game, 'manifest.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(sorted(lines, key=lambda line: line.split(' ', 2)[2])) + '\n')

    def summary(self, folder):
        return '%s: %d files copied, %d already there, %d MB in all' % (
            folder, self.copied, self.kept, self.size // (1024 * 1024))


def checksum(path):
    crc = 0
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            crc = zlib.crc32(block, crc)
    return crc & 0xffffffff


def prepare():
    """Prepares the game's data from original_game_1999/, as build.sh does."""
    if subprocess.call([sys.executable, os.path.join(HERE, 'prepare_data.py')]) != 0:
        sys.exit(1)


def remove_apple_double(folder):
    """macOS keeps files' extended attributes on a FAT drive as a ._ file
    beside each one, which no game has a use for."""
    if sys.platform != 'darwin':
        return
    beside = os.path.join(os.path.dirname(folder), '._' + os.path.basename(folder))
    if os.path.isfile(beside):
        os.remove(beside)
    for parent, _, files in os.walk(folder):
        for name in files:
            if name.startswith('._'):
                os.remove(os.path.join(parent, name))
