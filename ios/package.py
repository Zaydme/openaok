"""Makes the folder of game data the iOS app plays from, from your own copy of
the game.

Copy the CD's files into original_game_1999/ (see the main README), then run:

    python3 ios/package.py [--out DIR]

It prepares the game's data as build.sh does, and writes DIR/OpenAoK-data/:
the game's data, laid out as the source tree is, with manifest.txt, every
file's checksum, size and path, which the app checks as it copies it in
(tools/data/package_data.py). DIR is ios/data by default.

Copy OpenAoK-data to the iPhone or iPad -- AirDrop, the Files app, Finder over
USB, or the app's own folder -- then open OpenAoK and choose it. The app copies
it into the app's Documents folder; once it has, the folder can be deleted from
the device.

Everything it writes holds your copy of the game: keep it to your own devices.
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
    parser.add_argument('--out', default=os.path.join(HERE, 'data'),
                        help='where to write OpenAoK-data/')
    args = parser.parse_args()

    package_data.prepare()
    folder = os.path.join(args.out, 'OpenAoK-data')
    packer = package_data.Packer()
    packer.lay_out(folder)
    package_data.remove_apple_double(folder)
    print('==> ' + packer.summary(folder))
    print('Copy OpenAoK-data to the device, open OpenAoK and choose the folder.')


if __name__ == '__main__':
    main()
