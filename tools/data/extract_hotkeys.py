#!/usr/bin/env python3
"""Extracts the default hotkeys from the original EMPIRES2.EXE.

Reads original_game_1999/GAME/EMPIRES2.EXE and assets/extracted_data/strings.json.
Writes assets/extracted_data/hotkeys.json.

The defaults exist only in the executable (a player's `.hki` holds only what
they changed). Two set-up routines build the table, each a long run of
immediate pushes and a call:

    push string; push item; push group; mov ecx, esi; call NAME_ITEM
        names an item: its LANGUAGE.DLL string ("Go to Town Center")
    push letter; push shift; push alt; push ctrl; push key;
    push item; push group; call SET_DEFAULT
        the item's default key: a Windows virtual-key code, the modifiers, and
        the LANGUAGE.DLL string that spells the key's letter (-1 for keys that
        are not letters)

Key codes 253, 252 and 251 are the middle mouse button, wheel up and wheel
down; the Town Centre, the last notification and the next idle villager have a
second binding on those.

Each item is addressed as (group, item): group 0 is the unit commands, 1 the
game commands, 2 scrolling, 3 building, 4 the Town Centre, 5 the Dock, 6 the
Barracks, 7 the Archery Range, 8 the Stable, 9 the Siege Workshop, 10 the
Monastery, 11 the Market, 12 the military commands, 13 the Castle. Control
groups (Ctrl and a digit makes one, the digit selects it) are not in the table.

Usage:  python3 tools/data/extract_hotkeys.py
"""

import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata  # noqa: E402

ROOT = gamedata.ROOT
EXE = os.path.join(ROOT, 'original_game_1999', 'GAME', 'EMPIRES2.EXE')
STRINGS = os.path.join(ROOT, 'assets', 'extracted_data', 'strings.json')
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'hotkeys.json')

IMAGE_BASE = 0x400000
NAME_ITEM = 0x4730d0      # (group, item, string)
SET_DEFAULT = 0x472ef0    # (group, item, key, ctrl, alt, shift, letter)


def calls(blob, target, start, end):
    """Every run of immediate pushes ending in `call target` in [start, end):
    the pushed values, first argument first."""
    found = []
    pos = start
    while pos < end - 5:
        if blob[pos] != 0xE8:
            pos += 1
            continue
        rel = struct.unpack_from('<i', blob, pos + 1)[0]
        if IMAGE_BASE + pos + 5 + rel != target:
            pos += 1
            continue
        # Walk back over the pushes, skipping `mov ecx, esi` and `mov esi, ecx`
        # wherever they fall. .text is mapped at its file offset, so a file
        # position is its address less the image base.
        args = []
        back = pos
        while True:
            if blob[back - 2:back] in (b'\x8b\xce', b'\x8b\xf1'):
                back -= 2
            elif blob[back - 2] == 0x6A:
                args.append(struct.unpack_from('<b', blob, back - 1)[0])
                back -= 2
            elif blob[back - 5] == 0x68:
                args.append(struct.unpack_from('<i', blob, back - 4)[0])
                back -= 5
            else:
                break
        found.append(args)
        pos += 5
    return found


def main():
    blob = open(EXE, 'rb').read()
    strings = json.load(open(STRINGS))
    # The hotkey set-up code, in .text.
    start, end = 0x139000, 0x13d000
    names = {}
    for args in calls(blob, NAME_ITEM, start, end):
        if len(args) >= 3:
            group, item, string = args[0], args[1], args[2]
            names[(group, item)] = string
    defaults = []
    for args in calls(blob, SET_DEFAULT, start, end):
        if len(args) < 7:
            continue
        group, item, key, ctrl, alt, shift, letter = args[:7]
        string = names.get((group, item), -1)
        defaults.append({
            'group': group,
            'item': item,
            'string': string,
            'name': strings.get(str(string), ''),
            'key': key & 0xFF,
            'ctrl': bool(ctrl),
            'alt': bool(alt),
            'shift': bool(shift),
            'letter_string': letter,
            'letter': strings.get(str(letter), '') if letter >= 0 else '',
        })
    if len(defaults) != 120 or len(names) != 121:
        sys.exit('expected 120 defaults and 121 names, found %d and %d'
                 % (len(defaults), len(names)))
    with open(OUT, 'w') as fh:
        json.dump({'defaults': defaults}, fh, indent=1)
        fh.write('\n')
    print('wrote %s: %d default hotkeys' % (OUT, len(defaults)))


if __name__ == '__main__':
    main()
