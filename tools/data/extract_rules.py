"""Extracts the rules the game is played by: units, technologies and effects.

Reads the dat and assets/extracted_data/strings.json. Writes:

    assets/extracted_data/rules.json   units (hit points, attack and armour by
                                       class, speed, reload, range, costs, train
                                       time, ...), techs, effects, civs and
                                       terrain restrictions
    assets/extracted_data/media.json   graphic deltas and the sound table

The engine starts every player from a copy of these unit rules and lets
effects change that copy, one unit table per player.

Resource ids stay the dat's own throughout (0 food, 1 wood, 2 stone, 3 gold,
4 population headroom, 6 current age, ...): costs, storage and effects all share
that numbering.

Fractional values are written to thousandths, the precision at which the
engine converts them to fixed point on load.

Usage:
    python3 tools/data/extract_rules.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile  # noqa: E402
import gamedata  # noqa: E402

OUT = gamedata.path('rules.json')
MEDIA_OUT = gamedata.path('media.json')

# Help strings are stored in the dat at an id 79000 above the one LANGUAGE.DLL
# holds them under, as for units' help in extract_interface_data.py.
HELP_ID_OFFSET = 79000


def paid(flag):
    """Whether a cost slot is deducted. The low byte is the flag; the high bits
    carry unrelated editor state."""
    return 1 if (flag & 0xFF) else 0


def unit_rules(unit, strings):
    out = {
        'name': unit['name'],
        'label': strings.get(str(unit['name_string']), ''),
        'type': unit['type'],
        'class': unit['class'],
        'hp': unit['hp'],
        'los': unit['los'],
        'garrison': unit['garrison'],
        'size': unit['size'],
        'speed': unit['speed'],
        'rotation': unit['rotation'],
        'enabled': unit['enabled'],
        # The scenario editor's unit lists leave out hidden units and keep
        # heroes on a list of their own.
        'hide_in_editor': unit['hide_in_editor'],
        'hero': unit['hero'],
        'dead_unit': unit['dead_unit'],
        'dying_graphic': unit['dying_graphic'],
        'standing': unit['standing'],
        'walking': unit['walking'],
        'terrain_restriction': unit['terrain_restriction'],
        'placement_side_terrain': unit['placement_side_terrain'],
        'placement_terrain': unit['placement_terrain'],
        'hill_mode': unit['hill_mode'],
        'fog_visibility': unit['fog_visibility'],
        'interaction': unit['interaction'],
        'combat_level': unit['combat_level'],
        'minimap_mode': unit['minimap_mode'],
        'carry': unit['carry'],
        'decay': unit['decay'],
        'storage': [[s[0], s[1], s[2]] for s in unit['storage']],
        'damage_graphics': unit['damage_graphics'],
        'sounds': unit['sounds'],
        'search_radius': unit['search_radius'],
        'work_rate': unit['work_rate'],
        'attacks': unit['attacks'],
        'armours': unit['armours'],
        'base_armour': unit['base_armour'],
        'terrain_defense': unit['terrain_defense'],
        'max_range': unit['max_range'],
        'min_range': unit['min_range'],
        'blast_width': unit['blast_width'],
        'blast_level': unit['blast_level'],
        'reload': unit['reload'],
        'projectile': unit['projectile'],
        'accuracy': unit['accuracy'],
        'dispersion': unit['dispersion'],
        'frame_delay': unit['frame_delay'],
        'displacement': unit['displacement'],
        'attack_graphic': unit['attack_graphic'],
        'missile': unit['missile'],
        'costs': [[c[0], c[1], paid(c[2])] for c in unit['costs']],
        'train_time': unit['train_time'],
        'train_loc': unit['train_loc'],
        'button': unit['button'],
        'missiles': unit['missiles'],
        'max_missiles': unit['max_missiles'],
        'missile_area': unit['missile_area'],
        'secondary_projectile': unit['secondary_projectile'],
        'garrison_graphic': unit['garrison_graphic'],
        'construction_graphic': unit['construction_graphic'],
        'tech': unit['tech'],
        'head_unit': unit['head_unit'],
        'stack_unit': unit['stack_unit'],
        'transform_unit': unit['transform_unit'],
        'foundation_terrain': unit['foundation_terrain'],
        'garrison_type': unit['garrison_type'],
        'garrison_heal': unit['garrison_heal'],
        # 1 when the buildings beside it choose which of its graphic's angles
        # it shows: a wall's straight runs, its diagonals and its posts.
        'adjacent_mode': unit['adjacent_mode'],
        'icon': unit['icon'],
        'tasks': unit['tasks'],
    }
    return out


def tech_rules(tech, strings):
    return {
        'name': tech['name'],
        'label': strings.get(str(tech['name_string']), ''),
        'help': strings.get(str(tech['help_string'] - HELP_ID_OFFSET), ''),
        'required': tech['required'],
        'required_count': tech['required_count'],
        'costs': [[c[0], c[1], paid(c[2])] for c in tech['costs']],
        'civ': tech['civ'],
        'full_tech_mode': tech['full_tech_mode'],
        'location': tech['location'],
        'time': tech['time'],
        'effect': tech['effect'],
        'type': tech['type'],
        'icon': tech['icon'],
        'button': tech['button'],
    }


def main():
    dat = datfile.load()
    strings = gamedata.load('strings.json')

    units = {k: unit_rules(v, strings) for k, v in dat['rules_units'].items()}
    # The ground a building claims, and the annex units it is assembled from
    # -- a gate's two posts -- as the dat's object table has them. The
    # clearance is a half-extent per axis; a gate is four tiles one way and
    # one the other.
    for k, unit in units.items():
        obj = dat['units'].get(k, {})
        unit['clearance'] = obj.get('clearance', [0.0, 0.0])
        unit['annexes'] = [[a['unit'], a['dx'], a['dy']] for a in obj.get('annexes', [])]
    techs = {k: tech_rules(v, strings) for k, v in dat['techs'].items()}
    effects = {k: {'name': v['name'], 'commands': v['commands']}
               for k, v in dat['effects'].items()}
    civs = [{
        'id': c['id'],
        'name': c['name'],
        'tech_tree': c['tech_tree'],
        'team_bonus': c['team_bonus'],
        'resources': c['resources'],
        'enabled': c['enabled'],
    } for c in dat['civ_rules']]

    # Where each kind of unit may go and build, per terrain: thousandths of
    # the dat's figure -- 0 impassable, above 0 passable, above 50 buildable.
    restrictions = [[int(round(v * 1000)) for v in r] for r in dat['terrain_restrictions']]

    with open(OUT, 'w') as fh:
        json.dump({'units': units, 'techs': techs, 'effects': effects,
                   'civs': civs, 'terrain_restrictions': restrictions},
                  fh, separators=(',', ':'), sort_keys=True)
    print('wrote %s: %d units, %d techs, %d effects, %d civs'
          % (OUT, len(units), len(techs), len(effects), len(civs)))

    # The art and sound the rules point at: graphic deltas (composite
    # graphics such as a burning building's flames) and the sound table.
    media = {'graphics': dat['graphic_media'], 'sounds': dat['sounds']}
    with open(MEDIA_OUT, 'w') as fh:
        json.dump(media, fh, separators=(',', ':'), sort_keys=True)
    print('wrote %s: %d graphics with deltas or sound, %d sounds'
          % (MEDIA_OUT, len(media['graphics']), len(media['sounds'])))


if __name__ == '__main__':
    main()
