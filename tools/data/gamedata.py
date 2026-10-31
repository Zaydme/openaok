"""Shared paths and loaders for the JSON tables in assets/extracted_data."""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, 'assets', 'extracted_data')


def path(name):
    return os.path.join(DATA, name)


def load(name):
    with open(path(name), encoding='utf-8') as fh:
        return json.load(fh)


def load_annexes():
    """Building composition: annexes.dat.json with annexes.overrides.json on top.

    A building present in the overrides has its piece list replaced outright,
    as in ConfigManager::LoadAnnexes.
    """
    merged = {}
    for name in ('annexes.dat.json', 'annexes.overrides.json'):
        try:
            layer = load(name)
        except FileNotFoundError:
            continue
        for key, pieces in layer.items():
            if key.startswith('_'):   # header comments
                continue
            merged[key] = pieces
    return merged
