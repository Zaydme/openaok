"""Extracts the technology tree screen's table from the dat.

The dat carries a table of its own for the tech tree screen (genieutils'
`DatFile::TechTree`): the four ages, and for every building, unit and
research shown on the screen what it holds, what leads to it (the "slots":
up to ten other records, each with its mode -- 0 an age, 1 a building, 2 a
unit, 3 a research) and layout hints. It is written out record for record,
unchanged, in the order the dat stores it; the order matters, because the
screen walks the records in it.

The layout hints (`location`, `vertical_line`, `units_techs_total`,
`units_techs_first`) are written as the dat has them; the engine works them
out again for the civilisation shown (`src/screens/TechTreeLayout.cpp`).

Beside each record come the strings and icon the screen shows, from the unit
and technology tables:

- a building or unit: `name_string` (the unit's LanguageDLLName) and `icon`
  (its IconID), from the first playable civilisation's unit table;
- a research: `name_string` (the technology's LanguageDLLName) and `icon`.

The label the screen prints is LANGUAGE.DLL string `name_string + 9000` for a
building or unit and `name_string + 10000` for a research -- the tech tree's
own wording, with its line break ("Hand\\nCannoneer"); the record's `name`
(the dat's internal name) is the fallback when that string is missing.

Output: assets/extracted_data/techtree.json

Usage:
    python3 tools/data/extract_techtree.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile  # noqa: E402
import gamedata  # noqa: E402

OUT = gamedata.path('techtree.json')


def slots(record):
    """The record's connections, as [mode, id] pairs, the used ones only."""
    return [[record['slot_modes'][i], record['slot_ids'][i]] for i in range(record['slots'])]


def main():
    dat = datfile.load()
    tree = dat['tech_tree']
    units = dat['units']
    techs = dat['techs']

    def unit_strings(unit_id):
        unit = units.get(str(unit_id), {})
        return {'name': unit.get('name', ''), 'name_string': unit.get('name_string', -1),
                'icon': unit.get('icon', -1)}

    def tech_strings(tech_id):
        tech = techs.get(str(tech_id), {})
        return {'name': tech.get('name', ''), 'name_string': tech.get('name_string', -1),
                'icon': tech.get('icon', -1)}

    ages = [{
        'id': a['id'], 'status': a['status'], 'line_mode': a['line_mode'],
        'buildings': a['buildings'], 'units': a['units'], 'techs': a['techs'],
        'slots': slots(a),
    } for a in tree['ages']]
    buildings = [dict({
        'id': b['id'], 'status': b['status'], 'line_mode': b['line_mode'],
        'enabling_research': b['enabling_research'],
        'buildings': b['buildings'], 'units': b['units'], 'techs': b['techs'],
        'slots': slots(b), 'location': b['location_in_age'],
        'units_techs_total': b['units_techs_total'],
        'units_techs_first': b['units_techs_first'],
    }, **unit_strings(b['id'])) for b in tree['buildings']]
    tree_units = [dict({
        'id': u['id'], 'status': u['status'], 'line_mode': u['line_mode'],
        'enabling_research': u['enabling_research'],
        'upper_building': u['upper_building'], 'required_research': u['required_research'],
        'units': u['units'], 'slots': slots(u),
        'vertical_line': u['vertical_line'], 'location': u['location_in_age'],
    }, **unit_strings(u['id'])) for u in tree['units']]
    researches = [dict({
        'id': r['id'], 'status': r['status'], 'line_mode': r['line_mode'],
        'upper_building': r['upper_building'],
        'buildings': r['buildings'], 'units': r['units'], 'techs': r['techs'],
        'slots': slots(r), 'vertical_line': r['vertical_line'], 'location': r['location_in_age'],
    }, **tech_strings(r['id'])) for r in tree['researches']]

    with open(OUT, 'w') as fh:
        json.dump({'ages': ages, 'buildings': buildings, 'units': tree_units,
                   'researches': researches}, fh, separators=(',', ':'))
    print('wrote %s: %d ages, %d buildings, %d units, %d researches'
          % (OUT, len(ages), len(buildings), len(tree_units), len(researches)))


if __name__ == '__main__':
    main()
