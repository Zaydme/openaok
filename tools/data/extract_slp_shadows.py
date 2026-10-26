"""Maps each graphic in graphics.drs to its shadow graphic.

Trees, buildings and missiles each have a separate shadow graphic whose frames
are made entirely of SLP shadow-command pixels (cmd 0x0B). A graphic named
"<base>N<civ>" or "<base>NN" has its shadow at the same name with the part
marker replaced by '0': FOAK_NN -> FOAK_N0, RTWC1NNG -> RTWC1N0G.

Reads the given graphics.drs and assets/extracted_data/graphics.json.

Output:
  assets/extracted_data/shadows.json   {main_slp_id: shadow_slp_id}

Only the mapping is written. The engine decodes the shadow pixels itself
through HAL::GraphicCache, and the frames carry their own hotspots.

Usage:  python3 tools/data/extract_slp_shadows.py <graphics.drs>
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slp  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GRAPHICS_JSON = os.path.join(ROOT, 'assets', 'extracted_data', 'graphics.json')
SHADOW_MAP = os.path.join(ROOT, 'assets', 'extracted_data', 'shadows.json')


def shadow_name_candidates(name):
    """Names the shadow variant of `name` could take.

    The part marker is an 'N' somewhere in the last three characters; the
    shadow variant replaces it with '0'. Covers FOAK_NN -> FOAK_N0,
    CRCH3NNM -> CRCH3N0M and WCTW1NNGE -> WCTW1N0GE alike.
    """
    out = []
    for pos in (1, 2, 3):
        if len(name) >= pos and name[-pos] == 'N':
            out.append(name[:len(name) - pos] + '0' + name[len(name) - pos + 1:])
    return out


def main():
    drs_path = sys.argv[1]
    blob, entries = slp.read_drs(drs_path)
    graphics = json.load(open(GRAPHICS_JSON))
    by_name = {v['name']: v for v in graphics.values() if v.get('name')}

    # Pass 1: find every SLP that contains shadow pixels.
    shadow_slps = set()
    for slp_id in sorted(entries):
        base, _size = entries[slp_id]
        try:
            _ver, _props, headers = slp.slp_frame_headers(blob, base)
        except Exception:
            continue
        for hdr in headers:
            try:
                _idx, _a, _pc, shadow, _ol = slp.decode_frame(blob, base, hdr)
            except slp.SlpDecodeError:
                continue
            if any(shadow):
                shadow_slps.add(slp_id)
                break

    # Pass 2: map each main graphic onto its shadow graphic by name.
    mapping = {}
    for name, entry in by_name.items():
        slp_id = entry.get('slp_id', -1)
        if slp_id < 0 or slp_id not in entries or slp_id in shadow_slps:
            continue
        for candidate in shadow_name_candidates(name):
            other = by_name.get(candidate)
            if other and other.get('slp_id', -1) in shadow_slps:
                mapping[str(slp_id)] = other['slp_id']
                break

    with open(SHADOW_MAP, 'w') as fh:
        json.dump(mapping, fh, separators=(',', ':'), sort_keys=True)

    print('shadow SLPs: %d   mapped graphics: %d'
          % (len(shadow_slps), len(mapping)))
    print('wrote %s' % SHADOW_MAP)


if __name__ == '__main__':
    main()
