"""Lists the game's data files for the web page: web/data.json.

Run from the repository root after the data is prepared:

    python3 web/make_manifest.py

Each entry is a path under the repository root and its size. The page fetches
them from the same server and writes them into the browser's file system.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# What the game reads while it runs: the prepared data and the CD's fonts.
TREES = ['assets', os.path.join('original_game_1999', 'SYSTEM', 'FONTS')]

files = []
for tree in TREES:
    base = os.path.join(ROOT, tree)
    if not os.path.isdir(base):
        sys.exit('missing ' + base)
    for dirpath, _, names in os.walk(base):
        for name in names:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, ROOT).replace(os.sep, '/')
            files.append({'path': rel, 'size': os.path.getsize(full)})
files.sort(key=lambda f: f['path'])
with open(os.path.join(ROOT, 'web', 'data.json'), 'w') as out:
    json.dump(files, out)
print(len(files), 'files,', sum(f['size'] for f in files) // (1 << 20), 'MB')
