"""Makes the Switch's OpenAoK folder from your own copy of the game.

Copy the CD's files into original_game_1999/ (see the main README), then run:

    python3 switch/package.py [--nro PATH] [--out DIR]

It prepares the game's data as build.sh does, and writes

  DIR/switch/openaok/OpenAoK.nro      the game (build-switch/OpenAoK.nro, or --nro)
  DIR/switch/openaok/game/            the game's data, laid out as the source tree is
  DIR/switch/openaok/game/manifest.txt  every data file's checksum, size and path,
                                      which the game checks on its first start
                                      (tools/data/package_data.py)

DIR is build-switch/sd by default; give the SD card's own folder to write
straight to it. Files already there and unchanged are left alone, and what the
player keeps beside them (Profile, SaveGame, Scenario) is never touched.

Everything it writes holds your copy of the game: keep it to your own console.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'data'))
import package_data  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--nro', default=os.path.join(ROOT, 'build-switch', 'OpenAoK.nro'),
                        help='the game, built by switch/build.sh')
    parser.add_argument('--out', default=os.path.join(ROOT, 'build-switch', 'sd'),
                        help='where to write switch/openaok/: the SD card, or a folder to copy to it')
    args = parser.parse_args()

    package_data.prepare()
    folder = os.path.join(args.out, 'switch', 'openaok')
    packer = package_data.Packer()
    packer.lay_out(os.path.join(folder, 'game'))
    if os.path.exists(args.nro):
        packer.copy(args.nro, os.path.join(folder, 'OpenAoK.nro'))
    else:
        print('note: no %s yet; build it with switch/build.sh, or put a released OpenAoK.nro beside game/.'
              % os.path.relpath(args.nro, ROOT))
    package_data.remove_apple_double(folder)
    print('==> ' + packer.summary(folder))


if __name__ == '__main__':
    main()
