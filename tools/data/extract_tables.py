"""Writes the dat's graphic and unit tables.

Output:
  assets/extracted_data/graphics.json  {"<graphic id>": {name, filename, slp_id, frame_count, angle_count, layer}}
  assets/extracted_data/units.json     {"<unit id>": {name, id, type, class, hp, idle_graphic0, idle_graphic1,
                                                      dying_graphic, walking_graphic0, construction_graphic}}

Usage:  python3 tools/data/extract_tables.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'assets', 'extracted_data')


def main():
    dat = datfile.load()
    os.makedirs(OUT, exist_ok=True)
    for name, table in (('graphics.json', dat['graphic_table']), ('units.json', dat['unit_table'])):
        with open(os.path.join(OUT, name), 'w') as fh:
            json.dump(table, fh, indent=2)
        print('wrote %s (%d entries)' % (name, len(table)))


if __name__ == '__main__':
    main()
