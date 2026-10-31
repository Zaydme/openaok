"""Extracts the interface strings from LANGUAGE.DLL.

Reads original_game_1999/GAME/LANGUAGE.DLL, or the path given. Its Win32 string
resources hold every name, description and label the game prints, keyed by the
numeric ids the unit and tech tables reference (`LanguageDLLName`,
`LanguageDLLHelp`, and so on).

The DLL is a plain PE32 image, walked directly: DOS header -> PE header -> the
resource data directory -> the resource tree, keeping every leaf under type
RT_STRING (6). A string resource block holds sixteen consecutive strings, each
a UTF-16LE length-prefixed run, and block `n` covers ids `(n - 1) * 16` through
`(n - 1) * 16 + 15`; empty entries are holes rather than blank strings and are
dropped.

Output:
    assets/extracted_data/strings.json
      {"<string_id>": "<text>"}

Usage:
    python3 tools/data/extract_language.py [path/to/LANGUAGE.DLL]
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata  # noqa: E402

ROOT = gamedata.ROOT
OUT = os.path.join(ROOT, 'assets', 'extracted_data', 'strings.json')
DEFAULT_DLL = os.path.join(ROOT, 'original_game_1999', 'GAME', 'LANGUAGE.DLL')

RT_STRING = 6
STRINGS_PER_BLOCK = 16


class PeError(Exception):
    pass


def _sections(blob):
    """Return (resource_rva, [(virtual_address, size, raw_pointer), ...])."""
    if blob[:2] != b'MZ':
        raise PeError('not a DOS/PE image')
    pe_off = struct.unpack_from('<I', blob, 0x3C)[0]
    if blob[pe_off:pe_off + 4] != b'PE\0\0':
        raise PeError('missing PE signature')

    coff = pe_off + 4
    num_sections, = struct.unpack_from('<H', blob, coff + 2)
    opt_size, = struct.unpack_from('<H', blob, coff + 16)
    opt = coff + 20

    magic, = struct.unpack_from('<H', blob, opt)
    if magic == 0x10B:        # PE32
        dir_off = opt + 96
    elif magic == 0x20B:      # PE32+
        dir_off = opt + 112
    else:
        raise PeError('unknown optional header magic 0x%X' % magic)

    # Data directory entry 2 is the resource table.
    resource_rva, _resource_size = struct.unpack_from('<II', blob, dir_off + 2 * 8)

    sections = []
    table = opt + opt_size
    for i in range(num_sections):
        entry = table + i * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            '<IIII', blob, entry + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))
    return resource_rva, sections


def _to_offset(rva, sections):
    """Map a virtual address to a file offset."""
    for virtual_address, size, raw_pointer in sections:
        if virtual_address <= rva < virtual_address + size:
            return raw_pointer + (rva - virtual_address)
    raise PeError('address 0x%X is in no section' % rva)


def _string_blocks(blob):
    """Return {block_id: (offset, size)} for every RT_STRING resource."""
    resource_rva, sections = _sections(blob)
    base = _to_offset(resource_rva, sections)

    blocks = {}

    def walk(offset, depth, block_id):
        named, ids = struct.unpack_from('<HH', blob, offset + 12)
        entries = offset + 16
        for i in range(named + ids):
            name, child = struct.unpack_from('<II', blob, entries + i * 8)
            is_directory = bool(child & 0x80000000)
            child &= 0x7FFFFFFF

            if depth == 0:
                if name & 0x80000000 or name != RT_STRING:
                    continue
                next_id = block_id
            elif depth == 1:
                if name & 0x80000000:
                    continue      # string tables are numbered, never named
                next_id = name
            else:
                next_id = block_id

            if is_directory:
                walk(base + child, depth + 1, next_id)
            else:
                data_rva, size = struct.unpack_from('<II', blob, base + child)
                blocks.setdefault(next_id, (_to_offset(data_rva, sections), size))

    walk(base, 0, 0)
    return blocks


def extract(path):
    blob = open(path, 'rb').read()
    strings = {}
    for block_id, (offset, size) in _string_blocks(blob).items():
        cursor = offset
        end = offset + size
        for index in range(STRINGS_PER_BLOCK):
            if cursor + 2 > end:
                break
            length, = struct.unpack_from('<H', blob, cursor)
            cursor += 2
            if length == 0:
                continue          # a hole, not an empty string
            raw = blob[cursor:cursor + length * 2]
            cursor += length * 2
            text = raw.decode('utf-16-le', 'replace')
            strings[(block_id - 1) * STRINGS_PER_BLOCK + index] = text
    return strings


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL
    if not os.path.exists(path):
        sys.exit('no such file: %s' % path)

    strings = extract(path)
    with open(OUT, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump({str(k): v for k, v in sorted(strings.items())}, fh,
                  ensure_ascii=False, separators=(',', ':'), sort_keys=True)

    ids = sorted(strings)
    print('strings: %d  (ids %d..%d)' % (len(ids), ids[0], ids[-1]))
    for probe in (5083, 5121, 5109, 5070):
        if probe in strings:
            print('  %d = %r' % (probe, strings[probe]))
    print('wrote %s' % OUT)


if __name__ == '__main__':
    main()
