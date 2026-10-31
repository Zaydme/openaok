#!/usr/bin/env python3
"""Extracts the name of the network connection the multiplayer menu lists.

The 1999 game lists every DirectPlay service the machine has by the name
DirectPlay gives it, and beside the TCP/IP service its own "Local (LAN) TCP/IP
Connection" (LANGUAGE.DLL 9620). DirectX on the CD names the TCP/IP service in
DIRECTX/DPLAY.INF: the English strings' DESCA_TCP.

Reads original_game_1999/DIRECTX/DPLAY.INF.
Writes assets/extracted_data/connections.json: {"tcp": name}, the name empty
when the CD has no DirectX folder.

Usage:  python3 tools/data/extract_connections.py
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata  # noqa: E402

INF = os.path.join(gamedata.ROOT, 'original_game_1999', 'DIRECTX', 'DPLAY.INF')
OUT = gamedata.path('connections.json')


def english_string(text, key):
    """`key`'s value in the INF's [Strings.eng] section, or None."""
    section = None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('['):
            section = line.lower()
            continue
        if section != '[strings.eng]':
            continue
        found = re.match(r'(\w+)\s*=\s*"([^"]*)"', line)
        if found and found.group(1) == key:
            return found.group(2)
    return None


def main():
    name = ''
    if os.path.exists(INF):
        with open(INF, 'rb') as fh:
            name = english_string(fh.read().decode('latin-1'), 'DESCA_TCP') or ''
    else:
        print('note: original_game_1999/DIRECTX/DPLAY.INF not found; '
              'the multiplayer menu lists only the local network')
    with open(OUT, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump({'tcp': name}, fh, indent=1)
        fh.write('\n')
    print('wrote %s: %r' % (OUT, name))


if __name__ == '__main__':
    main()
