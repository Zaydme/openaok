"""Resolves every building's art by graphic name, per age and architecture set.

A building's `StandingGraphic` does not always name its body art (the dat
points the Mill at `MILL1N1G`, a different piece), so the art is matched by
name instead:

    <UNIT><age><part><arch>

`NN` is the building's body, `N0` its shadow, `D1`-`D3` its damage stages, and
`<arch>` is the architecture set -- `E`, `F`, `M`, `W`, or absent for a building
that looks the same for everyone. A `G` sits between the part and the set, and
the age digit is absent on units whose own name already carries one. A
civilisation's set is the letter its own unit table names for the Lumber Camp
(`SMIL`), which has one variant per set. Only art present in the DRS archives
is used.

Output:
    assets/extracted_data/buildings.json
      {"civ_arch": {"<civ_id>": "W"},
       "art": {"<unit_id>": {"<age>": {"<arch>": {"body": int, "shadow": int,
                                                  "damage": [int, ...]}}}}}

Usage:  python3 tools/data/extract_building_art.py
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile  # noqa: E402
import gamedata  # noqa: E402
import slp  # noqa: E402

ROOT = gamedata.ROOT
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'buildings.json')
ARCHIVES = ('graphics.drs', 'interfac.drs', 'terrain.drs')

# One graphic name: the unit it belongs to, which age of it, which part, and
# which architecture set. The age is optional because a unit called "MILL2"
# already carries its own; the trailing `G` is likewise not always there.
GRAPHIC_NAME = re.compile(r'^(?P<stem>[A-Z_]+)(?P<age>\d?)'
                          r'(?P<part>NN|N0|D1|D2|D3)G?(?P<arch>[EFMW]?)$')

PART_BODY = 'NN'
PART_SHADOW = 'N0'
DAMAGE_PARTS = ('D1', 'D2', 'D3')


def present_slps():
    have = set()
    for name in ARCHIVES:
        _blob, entries = slp.read_drs(
            os.path.join(ROOT, 'assets', 'original', 'Data', name))
        have |= set(entries)
    return have


def main():
    dat = datfile.load()
    graphics = gamedata.load('graphics.json')
    have = present_slps()

    # graphic name -> (id, slp), keeping only art present in the archives.
    by_name = {}
    for graphic_id, entry in graphics.items():
        name = entry.get('name')
        if not name or entry.get('slp_id', -1) not in have:
            continue
        by_name.setdefault(name, (int(graphic_id), entry['slp_id']))

    # Which architecture set each civ uses, taken from a building that has one
    # variant per set. Whichever letter this civ's table names is its set.
    probe = None
    for unit_id, entry in dat['units'].items():
        if entry['name'] == 'SMIL':
            probe = unit_id
            break
    civ_arch = {}
    if probe is not None:
        for civ_id, buildings in dat['civ_buildings'].items():
            graphic_id = buildings.get(probe)
            name = graphics.get(str(graphic_id), {}).get('name', '')
            civ_arch[civ_id] = name[-1] if name and name[-1] in 'EFMW' else ''

    # Everything the graphic table offers, indexed by what it is.
    catalogue = {}
    for name, (graphic_id, _slp_id) in by_name.items():
        match = GRAPHIC_NAME.match(name)
        if not match:
            continue
        key = (match.group('stem'), match.group('age') or '1',
               match.group('arch'))
        catalogue.setdefault(key, {})[match.group('part')] = graphic_id

    art = {}
    missing_body = []
    for unit_id, unit in dat['units'].items():
        # Buildings only: anything the dat gives a construction graphic or a
        # footprint is something that stands on the map.
        if unit['construction_graphic'] < 0 and unit['clearance'][0] <= 0:
            continue
        stem = unit['name']
        # A unit whose name ends in a digit carries its own age.
        own_age = stem[-1] if stem[-1:].isdigit() else None
        base = stem[:-1] if own_age else stem

        per_age = {}
        for (cat_stem, age, arch), parts in catalogue.items():
            if cat_stem != base and cat_stem != stem:
                continue
            if own_age and age != own_age:
                continue
            if PART_BODY not in parts:
                continue
            entry = {'body': parts[PART_BODY]}
            if PART_SHADOW in parts:
                entry['shadow'] = parts[PART_SHADOW]
            damage = [parts[part] for part in DAMAGE_PARTS if part in parts]
            if damage:
                entry['damage'] = damage
            per_age.setdefault(age, {})[arch] = entry

        if per_age:
            art[unit_id] = per_age
        elif unit['construction_graphic'] >= 0:
            missing_body.append(stem)

    with open(OUT, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump({'civ_arch': civ_arch, 'art': art}, fh,
                  separators=(',', ':'), sort_keys=True)

    print('buildings with body art: %d' % len(art))
    print('civ architecture sets: %s'
          % {k: v or 'generic' for k, v in sorted(civ_arch.items(), key=lambda kv: int(kv[0]))[:8]})
    if missing_body:
        print('no body art on this disc: %s' % ', '.join(sorted(set(missing_body))))
    print('wrote %s' % OUT)


if __name__ == '__main__':
    main()
