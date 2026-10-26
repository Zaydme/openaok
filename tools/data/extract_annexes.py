"""Extracts building annex composition from the dat.

A large building is a main unit plus annex units, each a unit with its own
graphic placed at an offset from the building's origin. The dat stores them in
`Unit.Building.Annexes` as {UnitID, Misplacement}. Annexes whose unit is not
in units.json, and annexes at zero offset, are left out.

Values are written raw; generate_annex_overrides.py turns a misplacement into
a tile offset.

Output:
  assets/extracted_data/annexes.dat.json
    {"<unit_id>": [{"unit": <annex_unit_id>, "dx": <tiles>, "dy": <tiles>}, ...]}

Usage:  python3 tools/data/extract_annexes.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UNITS_JSON = os.path.join(ROOT, 'assets', 'extracted_data', 'units.json')
# Generated; corrections go in annexes.overrides.json, which the engine loads on
# top of this file.
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'annexes.dat.json')


def main():
    units_1999 = json.load(open(UNITS_JSON))
    known = set(units_1999)
    dat = datfile.load()

    out = {}
    dropped = 0
    for unit_id, unit in dat['units'].items():
        annexes = []
        for annex in unit['annexes']:
            annex_id = annex['unit']
            if str(annex_id) not in known:
                dropped += 1   # Not in units.json.
                continue
            dx, dy = annex['dx'], annex['dy']
            if dx == 0 and dy == 0:
                continue
            annexes.append({'unit': annex_id, 'dx': float(dx), 'dy': float(dy)})
        if annexes and unit_id in known:
            out[unit_id] = annexes

    with open(OUT, 'w') as fh:
        json.dump(out, fh, separators=(',', ':'), sort_keys=True)

    total = sum(len(v) for v in out.values())
    print('buildings with annexes: %d   annex pieces: %d   dropped (not in 1999 data): %d'
          % (len(out), total, dropped))
    print('wrote %s' % OUT)


if __name__ == '__main__':
    main()
