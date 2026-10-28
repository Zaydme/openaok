"""Reads the original EMPIRES2.DAT.

The dat is parsed by genieutils (third_party/genieutils) through `dump_dat.cpp`,
which prints the tables the extractors use as JSON. This builds the dumper with
CMake on first use and keeps one parse per process.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))

EMPIRES2_DAT = os.path.join(ROOT, 'original_game_1999', 'GAME', 'DATA', 'EMPIRES2.DAT')
BUILD_DIR = os.path.join(ROOT, 'build', 'data-tools')
DUMPER = os.path.join(BUILD_DIR, 'dump_dat.exe' if os.name == 'nt' else 'dump_dat')

_cache = None


def build_dumper():
    """Builds dump_dat if it is missing or older than its source."""
    source = os.path.join(HERE, 'dump_dat.cpp')
    if os.path.exists(DUMPER) and os.path.getmtime(DUMPER) > os.path.getmtime(source):
        return
    try:
        subprocess.run(['cmake', '-S', HERE, '-B', BUILD_DIR, '-DCMAKE_BUILD_TYPE=Release'],
                       check=True, stdout=subprocess.DEVNULL)
        # A bounded number of jobs: a bare --parallel is unbounded under make.
        jobs = str(min(4, os.cpu_count() or 1))
        subprocess.run(['cmake', '--build', BUILD_DIR, '--target', 'dump_dat', '--parallel', jobs],
                       check=True, stdout=subprocess.DEVNULL)
    except FileNotFoundError:
        sys.exit('cmake not found: install CMake to build the data tools')
    except subprocess.CalledProcessError:
        sys.exit('building the dat dumper failed (it needs a C++ compiler, zlib and iconv)')


def load():
    """The parsed dat, as the dumper's JSON. Parsed once per process."""
    global _cache
    if _cache is None:
        build_dumper()
        output = subprocess.run([DUMPER, EMPIRES2_DAT], check=True,
                                capture_output=True, text=True).stdout
        _cache = json.loads(output)
    return _cache
