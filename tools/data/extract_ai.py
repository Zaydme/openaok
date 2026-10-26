"""Extracts the computer player's figures from the original AI scripts.

Reads the AI rule files (`.per`) in assets/original/Data/gamedata.drs, which
set "strategic numbers" and constants:

- file 60010: how many villagers to keep in each age (`civ-dark`,
  `civ-feudal`, `civ-castle`), one `#load-if-defined POPULATION-CAP-n` block
  per population cap, and the fishing fleet figures;
- file 60019: what share of the villagers gathers each resource, by age, by
  the resource in short supply (or none) and, in the Dark Age, by how many
  villagers there are.

Output: assets/extracted_data/ai.json

Usage:  python3 tools/data/extract_ai.py
"""
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata  # noqa: E402

GAMEDATA_DRS = os.path.join(gamedata.ROOT, 'assets', 'original', 'Data', 'gamedata.drs')
OUT = gamedata.path('ai.json')

CONSTANTS_FILE = 60010
GATHERERS_FILE = 60019
AGES = {'dark-age': 0, 'feudal-age': 1, 'castle-age': 2, 'imperial-age': 3}


def read_files():
    data = open(GAMEDATA_DRS, 'rb').read()
    tables = struct.unpack_from('<I', data, 56)[0]
    files = {}
    for t in range(tables):
        offset, count = struct.unpack_from('<II', data, 64 + t * 12 + 4)
        for i in range(count):
            file_id, at, size = struct.unpack_from('<III', data, offset + i * 12)
            files[file_id] = data[at:at + size].decode('latin1')
    return files


def strip_comments(text):
    return re.sub(r';[^\n]*', '', text)


def villager_targets(text):
    """civ-dark / civ-feudal / civ-castle per population cap."""
    targets = {}
    blocks = re.split(r'#load-if-defined\s+POPULATION-CAP-(\d+)', strip_comments(text))
    # blocks: [before, cap, body, cap, body, ...]
    for i in range(1, len(blocks) - 1, 2):
        cap = int(blocks[i])
        body = blocks[i + 1].split('#load-if-defined POPULATION-CAP')[0]
        consts = dict((m.group(1), int(m.group(2)))
                      for m in re.finditer(r'\(defconst\s+(civ-[a-z-]+)\s+(\d+)\)', body))
        if 'civ-dark' in consts:
            targets[str(cap)] = [consts['civ-dark'], consts.get('civ-feudal', consts['civ-dark']),
                                 consts.get('civ-castle', consts['civ-dark'])]
    return targets


def gatherer_shares(text):
    """The shares each age sets, by the resource it is short of (NO for none),
    in rule order."""
    shares = []
    for m in re.finditer(r'\(defrule(.*?)\n\)', strip_comments(text), flags=re.S):
        rule = ' '.join(m.group(1).split())
        need = re.search(r'\(goal resource-needed ([A-Z]+)\)', rule)
        if not need or 'sn-wood-gatherer-percentage' not in rule:
            continue
        age = re.search(r'\(current-age (==|>=) ([a-z-]+)\)', rule)
        if not age or age.group(2) not in AGES:
            continue
        entry = {'age': AGES[age.group(2)], 'and_later': age.group(1) == '>=',
                 'need': need.group(1)}
        civ = re.search(r'\(civilian-population (<|>=) (\d+)\)', rule)
        if civ:
            entry['civilians_' + ('below' if civ.group(1) == '<' else 'at_least')] = int(civ.group(2))
        for resource in ('wood', 'food', 'gold', 'stone'):
            value = re.search(r'set-strategic-number sn-%s-gatherer-percentage (\d+)' % resource, rule)
            if value:
                entry[resource] = int(value.group(1))
        shares.append(entry)
    return shares


def fishing(text):
    """The fishing fleet: the maps on which file 60010 loads "petersen fishboat"
    (file 60004), the smallest body of water a dock goes on, and the fishing
    ships to keep per population cap ([five-percent-pop, ten-percent-pop]:
    Dark Age, later ages)."""
    body = strip_comments(text)
    maps = []
    for m in re.finditer(r'#load-if-defined\s+([A-Z-]+)-MAP\s*\n(.*?)#end-if', body, flags=re.S):
        if '"petersen fishboat"' in m.group(2):
            maps.append(m.group(1).replace('-', ''))
    size = re.search(r'sn-minimum-water-body-size-for-dock\s+(\d+)', body)
    ships = {}
    blocks = re.split(r'#load-if-defined\s+POPULATION-CAP-(\d+)', body)
    for i in range(1, len(blocks) - 1, 2):
        part = blocks[i + 1].split('#load-if-defined POPULATION-CAP')[0]
        five = re.search(r'\(defconst\s+five-percent-pop\s+(\d+)\)', part)
        ten = re.search(r'\(defconst\s+ten-percent-pop\s+(\d+)\)', part)
        if five and ten:
            ships[blocks[i]] = [int(five.group(1)), int(ten.group(1))]
    return {'maps': sorted(set(maps)),
            'dock_water_tiles': int(size.group(1)) if size else 0,
            'fishing_ships': ships}


def main():
    files = read_files()
    out = {
        'villagers': villager_targets(files[CONSTANTS_FILE]),
        'gatherers': gatherer_shares(files[GATHERERS_FILE]),
        'fishing': fishing(files[CONSTANTS_FILE]),
    }
    with open(OUT, 'w') as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
    print('wrote %s: villager targets for %d caps, %d gatherer rules'
          % (OUT, len(out['villagers']), len(out['gatherers'])))


if __name__ == '__main__':
    main()
