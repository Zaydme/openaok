"""Extracts per-unit attack and work graphics, and per-graphic frame timing.

Reads the dat. A unit's work animations are in its task list, not its unit
record. Of a task's graphics, `ProceedingGraphicID` is the action itself (a
lumberjack's axe swing, a builder's hammering), `CarryingGraphicID` is the walk
with a load, and `WorkingGraphicID` is a per-target variant (a builder's is the
farm-planting graphic), used as a fallback. Frame durations are read per
graphic.

Output:
    assets/extracted_data/animations.json
      {"units":    {"<unit_id>": {"attack": <graphic_id>,
                                  "work":   <graphic_id>,
                                  "carry":  <graphic_id>,
                                  "work_range": <float, tiles>}},
       "graphics": {"<graphic_id>": {"frame_ms": <int>}}}

Usage:  python3 tools/data/extract_animation_data.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile  # noqa: E402
import gamedata  # noqa: E402

ROOT = gamedata.ROOT
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'animations.json')


def main():
    dat = datfile.load()
    graphics = gamedata.load('graphics.json')

    unit_out = {}
    for uid, unit in dat['units'].items():
        entry = {}

        attack = unit.get('attack_graphic', -1)
        # Kept only when graphics.json has that graphic.
        if attack >= 0 and str(attack) in graphics:
            entry['attack'] = attack

        # The first task with a graphic is the unit's work. A villager type has
        # exactly one such task: the job it is named for.
        for task in unit.get('tasks', []):
            work = task.get('proceeding', -1)
            # A hunt task (action 110) kills its target, then gathers the
            # carcass: its proceeding graphic is the kill, its working graphic
            # the gathering. Shepherds and hunters (VMSHE, VFHUN ...) gather
            # carcasses, so they use the working graphic; every other task
            # uses the proceeding one.
            working = task.get('working', -1)
            if (task.get('action') == 110 and unit.get('name', '')[2:] in ('SHE', 'HUN') and
                    working >= 0 and str(working) in graphics):
                work = working
            if work < 0 or str(work) not in graphics:
                # The per-target variant, when the main graphic is absent.
                work = task.get('working', -1)
            if work < 0 or str(work) not in graphics:
                continue
            entry['work'] = work
            entry['work_range'] = task.get('work_range', 0.0)
            carry = task.get('carrying', -1)
            if carry >= 0 and str(carry) in graphics:
                entry['carry'] = carry
            break

        if entry:
            unit_out[uid] = entry

    graphic_out = {}
    durations = []
    for gid, ms in dat['graphic_ms'].items():
        if gid not in graphics:
            continue
        if ms < 10 or ms > 1000:      # implausible; leave it to the default
            continue
        graphic_out[gid] = {'frame_ms': ms}
        durations.append(ms)

    with open(OUT, 'w') as fh:
        json.dump({'units': unit_out, 'graphics': graphic_out}, fh,
                  separators=(',', ':'), sort_keys=True)

    durations.sort()
    if durations:
        n = len(durations)
        print('frame times: min %d  p25 %d  median %d  p75 %d  max %d ms'
              % (durations[0], durations[n // 4], durations[n // 2],
                 durations[3 * n // 4], durations[-1]))
    attacks = sum(1 for e in unit_out.values() if 'attack' in e)
    works = sum(1 for e in unit_out.values() if 'work' in e)
    print('units with an attack graphic: %d   with a work graphic: %d   '
          'graphics with timing: %d' % (attacks, works, len(graphic_out)))
    for probe in ('118', '212', '123', '124'):
        if probe in unit_out:
            name = dat['units'][probe]['name']
            work = unit_out[probe].get('work', -1)
            print('  %-6s work graphic %s (%s)'
                  % (name, work, graphics.get(str(work), {}).get('name', '?')))
    print('wrote %s' % OUT)


if __name__ == '__main__':
    main()
