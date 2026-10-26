"""Prepares the game's data from an original Age of Empires II: The Age of Kings CD.

Copy the CD's files into original_game_1999/ (at least its GAME and SYSTEM
folders), then run:

    python3 tools/data/prepare_data.py [--force]

build.sh and the Android build run it themselves. It writes, all ignored by git:

  assets/original/                        the installed game, from GAME/EMPIRES2.CAB
  assets/extracted_data/                  the dat's and LANGUAGE.DLL's tables, as JSON
  assets/sprites/terrain/blendmasks.png   the terrain blending masks

and does nothing on later runs unless the CD's files change or --force is given.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CD = os.path.join(ROOT, 'original_game_1999')
ASSETS = os.path.join(ROOT, 'assets')
ORIGINAL = os.path.join(ASSETS, 'original')
STAMP = os.path.join(ASSETS, '.prepared')
STAMP_VERSION = 1

# Paths on the CD, as the CD spells them.
REQUIRED = [
    'GAME/EMPIRES2.CAB',
    'GAME/DATA/EMPIRES2.DAT',
    'GAME/LANGUAGE.DLL',
    'GAME/EMPIRES2.EXE',
    'SYSTEM/FONTS',
]
OPTIONAL = {
    'GAME/AVI': 'the movies',
    'GAME/SOUND': 'the music',
    'GAME/CAMPAIGN': 'the campaigns\' narration and pictures',
    'GOODIES/DEMO.SCN': 'the bonus scenario',
}
# The CD's folders the engine reads, whose names are kept in the CD's upper case.
UPPER_CASE_TREES = ['GAME', 'SYSTEM', 'GOODIES']

DAT_VERSION = 'VER 5.7'

EXTRACTORS = [
    ['extract_tables.py'],
    ['extract_language.py'],
    ['extract_rules.py'],
    ['extract_terrain_data.py'],
    ['extract_interface_data.py'],
    ['extract_animation_data.py'],
    ['extract_slp_shadows.py', os.path.join(ORIGINAL, 'Data', 'graphics.drs')],
    ['extract_annexes.py'],
    ['generate_annex_overrides.py', '--write'],
    ['extract_building_art.py'],
    ['extract_blendmasks.py'],
    ['extract_ai.py'],
    ['extract_techtree.py'],
    ['extract_hotkeys.py'],
]

OUTPUTS = [
    'assets/original/Data/graphics.drs',
    'assets/original/interfac_extracted/50500.bina',
    'assets/extracted_data/rules.json',
    'assets/extracted_data/strings.json',
    'assets/extracted_data/hotkeys.json',
    'assets/sprites/terrain/blendmasks.png',
]


def fail(message):
    sys.exit('error: ' + message)


def match_case(parent, name):
    """The entry of `parent` named `name` in any case, renamed to `name`."""
    exact = os.path.join(parent, name)
    if os.path.exists(exact):
        return exact
    if not os.path.isdir(parent):
        return None
    for entry in os.listdir(parent):
        if entry.lower() == name.lower():
            os.rename(os.path.join(parent, entry), exact)
            return exact
    return None


def upper_case_tree(path):
    """Renames every file and folder below `path` to upper case, as on the CD."""
    for parent, dirs, files in os.walk(path, topdown=False):
        for entry in dirs + files:
            if not entry.startswith('.') and entry != entry.upper():
                os.rename(os.path.join(parent, entry), os.path.join(parent, entry.upper()))


def find_on_cd(relative):
    path = CD
    for part in relative.split('/'):
        path = match_case(path, part)
        if path is None:
            return None
    return path


def check_cd():
    if not os.path.isdir(CD):
        fail('original_game_1999/ not found. Copy the files of your Age of Kings CD into it '
             '(see README.md).')
    for tree in UPPER_CASE_TREES:
        found = find_on_cd(tree)
        if found is not None:
            upper_case_tree(found)
    missing = [p for p in REQUIRED if find_on_cd(p) is None]
    if missing:
        fail('original_game_1999/ is missing:\n  ' + '\n  '.join(missing) +
             '\nCopy the whole CD (or a mounted image of it) into original_game_1999/.')
    for path, what in OPTIONAL.items():
        if find_on_cd(path) is None:
            print('note: original_game_1999/%s not found; the game runs without %s' % (path, what))


def fingerprint():
    parts = ['version %d' % STAMP_VERSION]
    for relative in REQUIRED[:3]:
        parts.append('%s %d' % (relative, os.path.getsize(os.path.join(CD, relative))))
    return '\n'.join(parts) + '\n'


def prepared():
    if not os.path.exists(STAMP):
        return False
    with open(STAMP) as fh:
        if fh.read() != fingerprint():
            return False
    return all(os.path.exists(os.path.join(ROOT, p)) for p in OUTPUTS)


def check_dat():
    sys.path.insert(0, HERE)
    import datfile  # noqa: E402
    version = datfile.load().get('version', '')
    if version != DAT_VERSION:
        fail('GAME/DATA/EMPIRES2.DAT is "%s", not the 1999 Age of Kings data (%s). '
             'The Conquerors and later editions are not supported.' % (version, DAT_VERSION))


def extract_cab():
    cab = os.path.join(CD, 'GAME', 'EMPIRES2.CAB')
    tool = shutil.which('cabextract')
    if tool is None:
        fail('cabextract is needed to unpack GAME/EMPIRES2.CAB. Install it with '
             '"brew install cabextract" (macOS) or "sudo apt install cabextract" (Debian/Ubuntu).')
    partial = ORIGINAL + '.partial'
    shutil.rmtree(partial, ignore_errors=True)
    os.makedirs(partial)
    subprocess.run([tool, '-q', '-d', partial, cab], check=True)
    shutil.rmtree(ORIGINAL, ignore_errors=True)
    os.rename(partial, ORIGINAL)


def extract_palette():
    sys.path.insert(0, HERE)
    import slp  # noqa: E402
    blob, entries = slp.read_drs(os.path.join(ORIGINAL, 'Data', 'interfac.drs'))
    offset, size = entries[50500]
    out = os.path.join(ORIGINAL, 'interfac_extracted')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, '50500.bina'), 'wb') as fh:
        fh.write(blob[offset:offset + size])


def run_extractors():
    os.makedirs(os.path.join(ASSETS, 'extracted_data'), exist_ok=True)
    os.makedirs(os.path.join(ASSETS, 'sprites', 'terrain'), exist_ok=True)
    for command in EXTRACTORS:
        print('  %s' % command[0])
        result = subprocess.run([sys.executable, os.path.join(HERE, command[0])] + command[1:],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0:
            print(result.stdout)
            fail('%s failed' % command[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--force', action='store_true', help='prepare again even if done')
    args = parser.parse_args()

    check_cd()
    if prepared() and not args.force:
        print('Game data already prepared.')
        return
    if os.path.exists(STAMP):
        os.remove(STAMP)
    print('Preparing the game data from original_game_1999/ ...')
    print('  unpacking GAME/EMPIRES2.CAB')
    extract_cab()
    extract_palette()
    print('  reading GAME/DATA/EMPIRES2.DAT')
    check_dat()
    run_extractors()
    with open(STAMP, 'w') as fh:
        fh.write(fingerprint())
    print('Game data ready.')


if __name__ == '__main__':
    main()
