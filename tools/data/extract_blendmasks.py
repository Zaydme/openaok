"""Extracts the terrain blending masks from blendomatic.dat.

Reads assets/original/Data/blendomatic.dat, which holds 31 alpha masks per
blending mode, one for each pattern of higher-priority neighbours around a
tile. Each mask is a 97x49 isometric diamond of alpha values, one terrain tile.

Format:
    header      uint32 mode_count, uint32 tile_count
    per mode    uint32 pixel_count, uint8 tile_flags[tile_count]
                uint8 bitmask_data[pixel_count * 4]     (32 bit-masks)
                uint8 alpha[pixel_count] * tile_count   (the alpha masks)

Pixels are stored as a diamond: row y holds 1 + 4y values in the top half and
mirrors in the bottom, totalling 2353 for a 49-row tile.

Output:
    assets/sprites/terrain/blendmasks.png   one 49px row per mode, 31 masks
                                            across, each 97px wide, alpha only

Usage:  python3 tools/data/extract_blendmasks.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamedata  # noqa: E402
import slp  # noqa: E402

ROOT = gamedata.ROOT
BLENDOMATIC = os.path.join(ROOT, 'assets', 'original', 'Data', 'blendomatic.dat')
OUT = os.path.join(ROOT, 'assets', 'sprites', 'terrain', 'blendmasks.png')


def diamond_rows(row_count):
    """Pixel count per row of the isometric diamond, top to bottom."""
    half = row_count // 2
    for y in range(row_count):
        if y < half:
            yield 1 + 4 * y
        else:
            yield ((row_count * 2) - 1) - 4 * (y - half)


def main():
    data = open(BLENDOMATIC, 'rb').read()
    mode_count, tile_count = struct.unpack_from('<II', data, 0)
    pos = 8

    modes = []
    for _ in range(mode_count):
        pixel_count = struct.unpack_from('<I', data, pos)[0]
        pos += 4
        pos += tile_count                      # tile_flags
        pos += pixel_count * 4                 # bit-masks, unused here
        tiles = []
        for _t in range(tile_count):
            tiles.append(data[pos:pos + pixel_count])
            pos += pixel_count
        modes.append((pixel_count, tiles))

    pixel_count = modes[0][0]
    row_count = int(pixel_count ** 0.5) + 1     # 49
    widths = list(diamond_rows(row_count))
    tile_w = max(widths)                        # 97
    assert sum(widths) == pixel_count, (sum(widths), pixel_count)

    W = tile_w * tile_count
    H = row_count * mode_count
    buf = bytearray(W * H * 4)

    for m, (_px, tiles) in enumerate(modes):
        for t, alpha in enumerate(tiles):
            read = 0
            for y, run in enumerate(widths):
                # Centre each row in the diamond.
                pad = (tile_w - run) // 2
                for x in range(run):
                    # Stored 0..128, where 0 means the neighbour covers this
                    # pixel completely and 128 means the base tile shows
                    # through. Emitted as straight coverage alpha.
                    a = alpha[read + x]
                    v = max(0, min(255, 255 - a * 2))
                    q = ((m * row_count + y) * W + t * tile_w + pad + x) * 4
                    buf[q] = 255
                    buf[q + 1] = 255
                    buf[q + 2] = 255
                    buf[q + 3] = v
                read += run

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    slp.write_png(OUT, W, H, buf)
    print('modes %d, tiles %d, %dx%d px per mask -> %s (%dx%d)'
          % (mode_count, tile_count, tile_w, row_count, OUT, W, H))


if __name__ == '__main__':
    main()
