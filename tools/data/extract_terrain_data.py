"""Extracts the terrain tables: tile art, map objects, and the names the map scripts use.

Reads:

  * the dat's terrain table: which SLP each terrain draws, its minimap colour,
    and which object grows on it (what makes forest ground a forest); also
    Gaia's map objects and the player colours;
  * assets/original/Data/terrain.drs, the tile art: SLPs of 97x49 isometric
    diamonds. The frames are not interchangeable variants: each set is a square
    tiling pattern, with a perfect-square frame count (100, 64, 36 or 9), and a
    tile draws the pattern cell for its position. The frame count is read from
    the archive;
  * assets/original/Data/gamedata.drs entry 54000, `random_map.def`, the header
    every random map script includes. Its `#const` block names the terrains,
    units and map objects, so a script saying `base_terrain GRASS3` resolves
    without any of those names being written into the engine;
  * assets/extracted_data/graphics.json.

ENGINE_TERRAIN says which of the dat's terrains each of the engine's
`TerrainType` values is, matched by name (the engine's Grass1 is the dat's
"Grass"); everything about the terrain then comes from that dat entry.

A terrain with no SLP of its own draws as another one: `Leaves` and the two
unused deserts are indirections, resolved here so the engine only sees a real
SLP.

Output:
    assets/extracted_data/terrain.json
      {"slps":      {"<slp_id>": {"frames": int}},
       "constants": {"terrain": {"GRASS": 0, ...},
                     "object":  {"OAKTREE": 349, ...},
                     "map":     {"ARABIA": 9, ...}},
       "engine":    {"<TerrainType name>": {"terrain": int, "slp": int,
                                            "minimap": int, "tree": int}},
       "object_art": {"<unit_id>": {"slp": int, "frame_count": int,
                                    "angle_count": int, "frame_ms": int,
                                    "minimap": int}},
       "player_ramps": [int, ...],
       "player_minimap": [int, ...],
       "scripts":   {"Arabia": 54201, ...},
       "gaia":      {"<unit_id>": {"resource": int, "amount": int}}}

Usage:  python3 tools/data/extract_terrain_data.py
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
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'terrain.json')
TERRAIN_DRS = os.path.join(ROOT, 'assets', 'original', 'Data', 'terrain.drs')
GAMEDATA_DRS = os.path.join(ROOT, 'assets', 'original', 'Data', 'gamedata.drs')
# random_map.def, the header every .rms includes.
RANDOM_MAP_DEF = 54000

# The `#const` block is split by comment banners into terrain, object and
# unit/building sections; these are the banners that start each.
SECTION_MARKERS = [
    ('terrain', 'TERRAIN TYPES'),
    ('object', 'OBJECT TYPES'),
    ('unit', 'UNITS'),
    ('building', 'BUILDINGS'),
]

# TerrainType -> the dat terrain it is, matched by name. The SLP, minimap colour
# and vegetation all follow from the dat entry named here.
ENGINE_TERRAIN = {
    'DeepWater':    22,   # Water 2
    'MediumWater':  23,   # Water 3
    'ShallowWater':  4,   # Shallows
    'Swamp':         1,   # Water
    'Beach':         2,
    'Sand':         13,   # Palm Desert; Desert itself draws as this
    'Dirt1':         6,   # Dirt
    'Dirt2':        11,   # Dirt 2
    'Dirt3':         3,   # Dirt 3
    'Grass1':        0,   # Grass
    'Grass2':       12,   # Grass 2
    'Grass3':        9,   # Grass 3
    'Grass4':        5,   # Leaves
    'Forest':       10,
    'Farm':          7,   # Farm1: planted
    'FarmDead':      8,   # Farm2: harvested out
    'FarmBuild1':   29,   # Farm Cnst1..3: a farm going up
    'FarmBuild2':   30,
    'FarmBuild3':   31,
    'Foundation':   27,   # under a Town Center, tower or wonder
    'Stone1':       24,   # Road
    'Stone2':       25,   # Road 2
    'Ice':          26,
}

# A terrain's vegetation is stored beside sparse decoration -- cacti dotted on
# dirt -- and only the dense entry is what the ground is made of.
VEGETATION_DENSITY = 1000

# Map objects whose standing graphic is not the art the map shows.
#
# Each resource has two graphics, an `_NN` and an `_N0`. For stone and forage
# the dat's standing graphic is the right one, but for gold it points at
# GOLDM_NN -- loose nuggets with no rock -- while the mine on the map is
# GOLDM_N0, grey rock veined with gold. No field in the dat distinguishes them,
# so the correction is named here by graphic.
OBJECT_ART_OVERRIDE = {'GOLDM': 'GOLDM_N0'}

# Minimap colours the unit table gets wrong or leaves out, by unit name.
#
# GOLDM's entry is the dull amber 65, while the ore-mine units that share its
# role carry 176, the bright gold the minimap shows. A relic has no colour in
# the dat, which leaves it off the minimap; 135 (plain white) is an addition.
OBJECT_MINIMAP_OVERRIDE = {'GOLDM': 176, 'RELIC': 135}

# dat resource id -> Core::ResourceType, the same remap the interface data uses.
RESOURCE_MAP = {0: 0, 1: 1, 2: 3, 3: 2}


def read_object_art(dat, graphics):
    """The art each map object stands as, from the dat's own unit table.

    A map object's frames are interchangeable shapes -- fourteen tree
    silhouettes, seven arrangements of gold -- so the count matters as much as
    the SLP; a generator picks one at random from it.
    """
    by_name = {}
    for graphic_id, entry in graphics.items():
        name = entry.get('name')
        if name:
            by_name.setdefault(name, (graphic_id, entry))

    out = {}
    for unit_id, entry in dat['gaia_art'].items():
        slp_id = entry['slp']
        frame_count = max(1, entry['frame_count'])
        angle_count = max(1, entry['angle_count'])
        frame_ms = int(round(entry['frame_duration'] * 1000.0))

        replacement = OBJECT_ART_OVERRIDE.get(entry['name'])
        if replacement is not None:
            found = by_name.get(replacement)
            if found is None:
                print('warning: %s names graphic %s, which graphics.json does '
                      'not have' % (entry['name'], replacement))
            else:
                _graphic_id, art = found
                slp_id = art['slp_id']
                frame_count = max(1, art.get('frame_count', 1))
                angle_count = max(1, art.get('angle_count', 1))

        out[unit_id] = {
            'slp': slp_id,
            # One frame per angle means the angles are interchangeable shapes
            # to pick from -- fourteen tree silhouettes, seven arrangements of
            # gold. More than one means the object plays an animation (sheep
            # grazing, a scout fidgeting).
            'frame_count': frame_count,
            'angle_count': angle_count,
            'frame_ms': frame_ms,
            # Its colour on the minimap, as a palette index. Zero means it is
            # not shown there at all, which is what trees carry.
            'minimap': OBJECT_MINIMAP_OVERRIDE.get(entry['name'],
                                                   entry['minimap']),
        }
    return out


def resolve_slp(terrains, terrain_id):
    """The SLP a terrain actually draws.

    Some have none of their own and point at another through `TerrainToDraw`;
    the chain is walked with a guard so a cycle in the data cannot hang this.
    """
    seen = set()
    key = str(terrain_id)
    while key in terrains and key not in seen:
        entry = terrains[key]
        if entry['slp'] >= 0:
            return entry['slp']
        seen.add(key)
        if entry['draw_as'] < 0:
            break
        key = str(entry['draw_as'])
    return -1


def read_slps():
    blob, entries = slp.read_drs(TERRAIN_DRS)
    out = {}
    for fid in sorted(entries):
        offset, _size = entries[fid]
        _version, _properties, frames = slp.slp_frame_headers(blob, offset)
        if not frames:
            continue
        out[str(fid)] = {'frames': len(frames)}
    return out


def gathered_as(dat):
    """What each stored resource is once gathered, from the gather tasks.

    Most objects store their stockpile directly -- a bush food (0), a mine
    gold -- but fish store "fish" (17), and it is the gatherer's task that says
    what a load of it is: the fishing ship's and the fisherman's gather tasks
    take resource 17 in and hand food (0) out. Read from every unit's tasks
    ([action, class, unit, resource in, range, value, resource out])."""
    out = {}
    for unit in dat['rules_units'].values():
        for task in unit.get('tasks') or []:
            if len(task) < 7 or task[0] != 5:
                continue
            taken, given = task[3], task[6]
            if taken >= 0 and given in RESOURCE_MAP and taken not in RESOURCE_MAP:
                out.setdefault(taken, given)
    return out


def read_gaia(dat, object_constants):
    """What each map object is worth, from the Gaia unit table."""
    gaia = dat['gaia_art']
    becomes = gathered_as(dat)
    out = {}
    for unit_id in sorted(set(object_constants.values())):
        unit = gaia.get(str(unit_id))
        if not unit:
            continue
        for rtype, amount in unit.get('storage') or []:
            rtype = rtype if rtype in RESOURCE_MAP else becomes.get(rtype, rtype)
            if rtype in RESOURCE_MAP and amount > 0:
                out[str(unit_id)] = {'resource': RESOURCE_MAP[rtype],
                                     'amount': int(amount)}
                break
    return out


def read_scripts(text):
    """The archive ids of the random map scripts.

    random_map.def opens with a comment listing every .rms and the entry it
    lives in.
    """
    out = {}
    for match in re.finditer(r'(\w+)\.rms\s+(\d+)', text):
        out[match.group(1).replace('_', ' ').title().replace(' ', '')] = \
            int(match.group(2))
    return out


def read_constants():
    blob, entries = slp.read_drs(GAMEDATA_DRS)
    offset, size = entries[RANDOM_MAP_DEF]
    text = blob[offset:offset + size].decode('latin1', 'replace')

    # Everything before the first banner is the map-type list.
    section = 'map'
    out = {'map': {}, 'terrain': {}, 'object': {}, 'unit': {}, 'building': {}}
    scripts = read_scripts(text)
    const = re.compile(r'^\s*#const\s+(\w+)\s+(-?\d+)')
    for line in text.split('\n'):
        for name, marker in SECTION_MARKERS:
            if marker in line:
                section = name
        match = const.match(line)
        if match:
            out[section][match.group(1)] = int(match.group(2))
    return out, scripts


def main():
    dat = datfile.load()
    terrains = dat['terrains']
    slps = read_slps()
    constants, scripts = read_constants()
    gaia = read_gaia(dat, constants['object'])
    object_art = read_object_art(dat, gamedata.load('graphics.json'))

    engine = {}
    for name, terrain_id in ENGINE_TERRAIN.items():
        entry = terrains.get(str(terrain_id))
        if entry is None:
            print('warning: %s names terrain %d, which the dat does not have'
                  % (name, terrain_id))
            continue
        slp_id = resolve_slp(terrains, terrain_id)
        if str(slp_id) not in slps:
            print('warning: %s (dat terrain %d "%s") draws SLP %d, which '
                  'terrain.drs does not have'
                  % (name, terrain_id, entry['name'], slp_id))
            continue

        record = {
            'terrain': terrain_id,
            'slp': slp_id,
            # Three palette indexes, brightest first: a minimap tile is shaded
            # by which way its ground slopes. Flat ground takes the middle one.
            'minimap': entry['minimap'][1],
            'minimap_shades': entry['minimap'],
            # Which ground draws over which where they meet, and the class of
            # transition it makes (land, farm, beach, water, shallows).
            'blend_priority': entry['blend_priority'],
            'blend_type': entry['blend_type'],
        }
        vegetation = sorted(entry['objects'], key=lambda o: -o['density'])
        if vegetation and vegetation[0]['density'] >= VEGETATION_DENSITY:
            record['tree'] = vegetation[0]['unit']
        engine[name] = record

    # Where each player's eight-entry colour ramp starts in the palette. The
    # order is not a simple stride: player 5 is at 96 and player 8 at 80.
    player_ramps = [c['base'] for c in dat['player_colours']]
    # The pure colour each player is drawn with on the minimap and the
    # selection outline: saturated entries (242 a true blue, 36 a true red),
    # not the mid-ramp shades a unit's body is painted with.
    player_minimap = [c['outline'] for c in dat['player_colours']]

    with open(OUT, 'w') as fh:
        json.dump({'slps': slps, 'constants': constants, 'engine': engine,
                   'scripts': scripts, 'gaia': gaia, 'object_art': object_art,
                   'player_ramps': player_ramps,
                   'player_minimap': player_minimap},
                  fh,
                  separators=(',', ':'), sort_keys=True)

    print('dat %s: %d terrains, %d graphics, %d sounds, %d techs, %d effects'
          % (dat['version'], dat['terrain_count'], dat['graphic_count'],
             dat['sound_count'], dat['tech_count'], dat['effect_count']))
    print('terrain SLPs in terrain.drs: %d' % len(slps))
    print('engine terrains resolved: %d  (with vegetation: %d)'
          % (len(engine), sum(1 for v in engine.values() if 'tree' in v)))
    for section in ('map', 'terrain', 'object', 'unit', 'building'):
        print('  %-9s constants: %d' % (section, len(constants[section])))
    print('random map scripts: %d' % len(scripts))
    print('gaia objects carrying a resource: %d' % len(gaia))
    print('map objects with art: %d  (corrected: %d, animated: %d, on the '
          'minimap: %d)'
          % (len(object_art), len(OBJECT_ART_OVERRIDE),
             sum(1 for v in object_art.values() if v['frame_count'] > 1),
             sum(1 for v in object_art.values() if v['minimap'] > 0)))
    print('player colour ramps: %s' % player_ramps)
    print('player minimap colours: %s' % player_minimap)
    print('wrote %s' % OUT)


if __name__ == '__main__':
    main()
