"""Extracts the data the HUD needs: icons, command-panel slots, costs and stats.

Reads the dat and assets/extracted_data/strings.json (from extract_language.py).
For each unit it writes the 36x36 icon that stands for it, its button in the
5x3 command grid, what it costs and which building produces it (all from the
unit table's `Creatable` block); the figures the stat panel prints beside a
portrait (hit points, attack, the two armours, line of sight); its gathering
and selection figures; and its name and help text. The unit table gives a
name's string id directly and a help text's id plus HELP_ID_OFFSET; units whose
ids resolve to nothing carry no name.

Resource ids are remapped from the dat's order (0 food, 1 wood, 2 stone,
3 gold) to `Core::ResourceType` (0 food, 1 wood, 2 gold, 3 stone) here, so the
engine never sees the dat's ordering.

Output:
    assets/extracted_data/interface.json
      {"icon_slp": {"unit": 50730, "building": 50705, "command": 50721, ...},
       "hud_slp_base": {"1280x1024": 51141, ...},
       "civs": [{"id": 1, "name": "British"}, ...],
       "labels": {"cost": "Cost: ", "hp": "Hit Pts: ", ...},
       "resource_names": ["Food", "Wood", "Gold", "Stone"],
       "units": {"<unit_id>": {"icon": int, "button": int, "train_loc": int,
                               "train_time": int, "population": int,
                               "costs": [[resource, amount], ...],
                               "hp": int, "attack": int, "melee_armour": int,
                               "pierce_armour": int, "range": int,
                               "line_of_sight": int, "interface_kind": int,
                               "footprint": int, "construction_graphic": int,
                               "dead_unit": int,
                               "name": str, "help": str}}}

`interface_kind` splits a villager's build menu into its two pages: economic
buildings are kind 2 and military buildings kind 10, so a House and a Barracks
can both have button 1.

Usage:  python3 tools/data/extract_interface_data.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata  # noqa: E402
import datfile  # noqa: E402

ROOT = gamedata.ROOT
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'interface.json')

# Icon sheets in interfac.drs, 36x36 frames. 50705-50708 are the building icons
# of the four architecture sets; 50705 (West European) is used. 50721 holds the
# command icons (build menus, formations, stances) and 50729 the research icons.
ICON_SLP = {'unit': 50730, 'building': 50705,
            'command': 50721, 'research': 50729}

# The panel art is per civilisation: interfac.drs holds one full-screen set per
# civ at each resolution, in the dat's civ order, so civ 1's 1280x1024 panel is
# 51141 and civ n's is 51141 + (n - 1).
HUD_SLP_BASE = {'800x600': 51101, '1024x768': 51121, '1280x1024': 51141}

# The playable civilisations, each with panel art (51141..51153 at 1280x1024).
AOK_CIV_COUNT = 13

# dat resource id -> Core::ResourceType
RESOURCE_MAP = {0: 0, 1: 1, 2: 3, 3: 2}
POPULATION_RESOURCE = 4

# The unit table stores a help string id offset from the one LANGUAGE.DLL
# actually holds; the DLL's id is this much lower.
HELP_ID_OFFSET = 79000

STRINGS = os.path.join(ROOT, 'assets', 'extracted_data', 'strings.json')

# LANGUAGE.DLL strings: the labels substituted into a help string's
# placeholders, and the resource names printed in a cost.
LABEL_STRING_ID = {
    'cost': 20200, 'hp': 20201, 'attack': 20202,
    'range': 20203, 'armor': 20204, 'piercearmor': 20205, 'garrison': 20206,
    # Shown under a building whose next unit is waiting for population room.
    'need_houses': 3075,   # "You need more houses to continue unit production."
}
# 4301..4304 in the dat's resource order (food, wood, stone, gold), reordered
# here into Core::ResourceType's.
RESOURCE_NAME_BASE = 4301

# Slots in a civ's starting resource table. These are the dat's own resource
# ids, the same numbering a unit's costs and a building's storage use.
FARM_FOOD_RESOURCE = 36      # "Farm Food Amount": what a farm holds when built
BUILDING_UNIT_TYPE = 80      # the dat's unit type for a building

# The four resources a player gathers, by dat id.
GATHERABLE = set(RESOURCE_MAP)


def gatherable_store(unit, farm_food):
    """What `unit` holds for the taking, as [resource, amount], or [].

    The first storage slot that is a gatherable resource (a unit's first slot
    is population).

    A farm, the one building that stores food, holds the civ's "Farm Food
    Amount" (resource 36, 175, raised by technologies), not its own storage
    figure (15).
    """
    for slot in unit.get('storage', []):
        rtype = slot['type']
        if rtype not in GATHERABLE or slot['amount'] <= 0:
            continue
        amount = int(round(slot['amount']))
        if unit.get('unit_type') == BUILDING_UNIT_TYPE and rtype == 0 and farm_food:
            amount = farm_food
        return [RESOURCE_MAP[rtype], amount]
    return []


def main():
    dat = datfile.load()
    dat_units = dat['units']

    strings = {}
    if os.path.exists(STRINGS):
        with open(STRINGS) as fh:
            strings = json.load(fh)
    else:
        print('warning: %s missing -- run extract_language.py for names'
              % os.path.basename(STRINGS))

    # Every civ starts with the same table, so the first civ's figure is the
    # farm food for all of them.
    civ_start = (dat.get('civ_resources') or [[]])[0]
    farm_food = (int(round(civ_start[FARM_FOOD_RESOURCE]))
                 if len(civ_start) > FARM_FOOD_RESOURCE else 0)

    out = {}
    for uid, unit in dat_units.items():
        # Nothing to say about a unit with no icon and no way to produce it.
        if unit['icon'] < 0 and unit['train_loc'] < 0:
            continue

        costs = []
        population = 0
        for rtype, amount in unit['costs']:
            if rtype == POPULATION_RESOURCE:
                population = amount
            elif rtype in RESOURCE_MAP:
                costs.append([RESOURCE_MAP[rtype], amount])

        button = unit['button']
        out[uid] = {
            'icon': unit['icon'],
            # 255 and 0 mean "no button" in the dat; written as -1.
            'button': -1 if button in (255, 0) else button,
            'train_loc': unit['train_loc'],
            'train_time': unit['train_time'],
            'population': population,
            'costs': costs,
            'hp': unit['hp'],
            'attack': unit['attack'],
            'melee_armour': unit['melee_armour'],
            'pierce_armour': unit['pierce_armour'],
            'range': unit['range'],
            'line_of_sight': unit['line_of_sight'],
            'interface_kind': unit['interface_kind'],
            # A building's footprint, in whole tiles, from the half-extent the
            # dat stores. Zero for anything that does not occupy ground.
            'footprint': max(1, int(round(unit['clearance'][0] * 2)))
                         if unit['clearance'][0] > 0 else 0,
            'construction_graphic': unit['construction_graphic'],
            # What it leaves behind when it comes down -- a building's rubble.
            'dead_unit': unit['dead_unit'],
            # Which buildings this unit may hand a load to: a lumberjack lists
            # the Town Centre and the Lumber Camp, a forager the Town Centre and
            # the Mill.
            'drop_sites': unit.get('drop_sites', []),
            # Resources per second this unit gathers: a lumberjack 0.39, a
            # forager 0.31.
            'work_rate': unit.get('work_rate', 0.0),
            # How much of a resource it can shoulder before walking a load
            # home: ten for a villager, thirty-five for a hunter.
            'carry_capacity': unit.get('carry_capacity', 0),
            # What this unit holds for the taking, as [resource, amount], such
            # as a farm's food.
            'stores': gatherable_store(unit, farm_food),
            # Population headroom it provides while it stands: five for a Town
            # Centre or a House, twenty for a Castle. The dat books this as a
            # positive amount of resource 4 in the building's storage; a unit
            # books the same resource negative.
            'headroom': sum(int(round(s['amount'])) for s in unit.get('storage', [])
                            if s['type'] == POPULATION_RESOURCE and s['amount'] > 0),
            'unit_type': unit.get('unit_type', 0),
            'unit_class': unit.get('unit_class', -1),
            # How it is marked when selected: the outline's radii in tiles, the
            # height its hit point bar floats at, the selection effect (whether
            # the bar and outline are shown) and the obstruction type (round or
            # square outline).
            'outline': unit.get('outline', [0.0, 0.0]),
            'hp_bar_height': unit.get('hp_bar_height', 0.0),
            'selection_effect': unit.get('selection_effect', 1),
            'obstruction': unit.get('obstruction', 0),
            'name': strings.get(str(unit['name_string']), ''),
            'help': strings.get(str(unit['help_string'] - HELP_ID_OFFSET), ''),
        }

    # Map objects (trees, gold mines, sheep), which belong to Gaia rather than
    # the playable civ's table and so are absent from the loop above. They have
    # no cost, button or trainer, so only the fields the panel reads are filled.
    for uid, obj in dat.get('gaia_art', {}).items():
        if uid in out:
            continue
        name = strings.get(str(obj.get('name_string', -1)), '')
        if not name:
            continue
        out[uid] = {
            'icon': obj.get('icon', -1),
            'button': -1,
            'train_loc': -1,
            'train_time': 0,
            'population': 0,
            'costs': [],
            'hp': obj.get('hp', 0),
            'attack': obj.get('attack', 0),
            'melee_armour': obj.get('melee_armour', 0),
            'pierce_armour': obj.get('pierce_armour', 0),
            'range': 0,
            'line_of_sight': obj.get('line_of_sight', 0),
            'interface_kind': 0,
            'footprint': 0,
            'construction_graphic': -1,
            'dead_unit': -1,
            'drop_sites': [],
            'work_rate': 0.0,
            'carry_capacity': 0,
            'stores': [],
            # The dat's unit type: 70 is a living unit (a sheep, a deer, a
            # boar), 10 a static object (a tree, a mine, a bush), whose hit
            # points are not shown.
            'unit_type': obj.get('unit_type', 0),
            'unit_class': -1,
            'headroom': 0,
            'outline': obj.get('outline', [0.0, 0.0]),
            'hp_bar_height': obj.get('hp_bar_height', 0.0),
            'selection_effect': obj.get('selection_effect', 1),
            'obstruction': obj.get('obstruction', 0),
            'name': name,
            'help': strings.get(
                str(obj.get('help_string', 0) - HELP_ID_OFFSET), ''),
        }

    labels = {key: strings.get(str(sid), '')
              for key, sid in LABEL_STRING_ID.items()}
    resource_names = ['', '', '', '']
    for dat_resource, engine_resource in RESOURCE_MAP.items():
        resource_names[engine_resource] = strings.get(
            str(RESOURCE_NAME_BASE + dat_resource), '')

    # Civ order matters: it is what indexes into the panel art.
    civs = [{'id': i + 1, 'name': name}
            for i, name in enumerate(dat['civs'][:AOK_CIV_COUNT])]

    with open(OUT, 'w') as fh:
        json.dump({'icon_slp': ICON_SLP, 'hud_slp_base': HUD_SLP_BASE,
                   'civs': civs, 'labels': labels,
                   'resource_names': resource_names, 'units': out}, fh,
                  separators=(',', ':'), sort_keys=True)

    buildable = sum(1 for v in out.values() if v['train_loc'] >= 0)
    with_button = sum(1 for v in out.values() if v['button'] >= 0)
    named = sum(1 for v in out.values() if v['name'])
    print('units with interface data: %d  (producible %d, on a button %d, named %d)'
          % (len(out), buildable, with_button, named))
    print('wrote %s' % OUT)


if __name__ == '__main__':
    main()
