"""Minimal reader for the DRS archive and SLP graphic formats, and a PNG writer.

Frame decoding keeps the three special pixel classes apart instead of
flattening every frame to plain RGBA:

  cmd 0x0B          -> shadow pixels
  cmd 0x06 / 0x0A   -> player colour pixels
  cmd 0x4E / 0x5E   -> player-colour outline (unit silhouette behind buildings)
"""
import struct

# --- DRS ---------------------------------------------------------------------


def read_drs(path):
    """Return (blob, {file_id: (offset, size)}) for a DRS archive."""
    blob = open(path, 'rb').read()
    # copyright[40] version[4] type[12] num_tables[4] first_file[4]
    num_tables, _first = struct.unpack_from('<II', blob, 56)
    entries = {}
    for t in range(num_tables):
        _ftype, toff, nfiles = struct.unpack_from('<4sII', blob, 64 + t * 12)
        for i in range(nfiles):
            fid, foff, fsize = struct.unpack_from('<III', blob, toff + i * 12)
            entries[fid] = (foff, fsize)
    return blob, entries


# --- Palette -----------------------------------------------------------------


# --- SLP ---------------------------------------------------------------------


def slp_frame_headers(blob, base):
    """Return (version, properties, [frame_header, ...]) without decoding pixels.

    Each frame header is a dict with the eight 32-bit fields the format
    defines; hotspot_x/hotspot_y are the texel that sits on the object's tile.
    """
    version = blob[base:base + 4].decode('latin1', 'replace')
    num_frames, properties = struct.unpack_from('<HH', blob, base + 4)
    frames = []
    for f in range(num_frames):
        off = base + 32 + f * 32
        (cmd_off, outline_off, pal_off, props,
         w, h, hx, hy) = struct.unpack_from('<IIIIIIii', blob, off)
        frames.append({
            'cmd_table_offset': cmd_off,
            'outline_table_offset': outline_off,
            'palette_offset': pal_off,
            'properties': props,
            'width': w, 'height': h,
            'hotspot_x': hx, 'hotspot_y': hy,
        })
    return version, properties, frames


class SlpDecodeError(Exception):
    pass


def decode_frame(blob, base, hdr):
    """Decode one 8-bit SLP frame.

    Returns (indexes, alpha, player_mask, shadow_mask, outline_mask) as flat
    bytearrays of width*height. `indexes` holds palette indexes; the masks are
    0/255 flags except player_mask, which stores the palette index of the
    player-colour pixel so its shade survives.
    """
    w, h = hdr['width'], hdr['height']
    n = w * h
    indexes = bytearray(n)
    alpha = bytearray(n)
    player = bytearray(n)
    shadow = bytearray(n)
    outline = bytearray(n)
    if w == 0 or h == 0:
        return indexes, alpha, player, shadow, outline

    # Outline table: per row, the transparent run at each edge.
    edges = struct.unpack_from('<%dH' % (h * 2), blob, base + hdr['outline_table_offset'])
    cmd_offsets = struct.unpack_from('<%dI' % h, blob, base + hdr['cmd_table_offset'])

    for row in range(h):
        left = edges[row * 2]
        right = edges[row * 2 + 1]
        if left == 0x8000 or right == 0x8000:
            continue  # Fully transparent row.

        p = base + cmd_offsets[row]
        col = left
        row_base = row * w

        while True:
            data = blob[p]; p += 1
            if data == 0x0F:
                break
            cmd = data & 0x0F

            if cmd in (0x0, 0x4, 0x8, 0xC):        # lesser block copy
                cnt = (data & 0xFC) >> 2
                for _ in range(cnt):
                    indexes[row_base + col] = blob[p]; p += 1
                    alpha[row_base + col] = 255
                    col += 1
            elif cmd in (0x1, 0x5, 0x9, 0xD):      # lesser skip
                col += (data & 0xFC) >> 2
            elif cmd == 0x2:                       # greater block copy
                cnt = ((data & 0xF0) << 4) + blob[p]; p += 1
                for _ in range(cnt):
                    indexes[row_base + col] = blob[p]; p += 1
                    alpha[row_base + col] = 255
                    col += 1
            elif cmd == 0x3:                       # greater skip
                cnt = ((data & 0xF0) << 4) + blob[p]; p += 1
                col += cnt
            elif cmd == 0x6:                       # copy, player colour
                cnt = (data & 0xF0) >> 4
                if cnt == 0:
                    cnt = blob[p]; p += 1
                for _ in range(cnt):
                    idx = blob[p]; p += 1
                    indexes[row_base + col] = idx
                    alpha[row_base + col] = 255
                    player[row_base + col] = idx if idx else 1
                    col += 1
            elif cmd == 0x7:                       # run of plain colour
                cnt = (data & 0xF0) >> 4
                if cnt == 0:
                    cnt = blob[p]; p += 1
                idx = blob[p]; p += 1
                for _ in range(cnt):
                    indexes[row_base + col] = idx
                    alpha[row_base + col] = 255
                    col += 1
            elif cmd == 0xA:                       # run, player colour
                cnt = (data & 0xF0) >> 4
                if cnt == 0:
                    cnt = blob[p]; p += 1
                idx = blob[p]; p += 1
                for _ in range(cnt):
                    indexes[row_base + col] = idx
                    alpha[row_base + col] = 255
                    player[row_base + col] = idx if idx else 1
                    col += 1
            elif cmd == 0xB:                       # shadow pixels
                cnt = (data & 0xF0) >> 4
                if cnt == 0:
                    cnt = blob[p]; p += 1
                for _ in range(cnt):
                    shadow[row_base + col] = 255
                    col += 1
            elif cmd == 0xE:                       # extended
                if data == 0x4E:
                    outline[row_base + col] = 255
                    col += 1
                elif data == 0x6E:                 # shield outline
                    col += 1
                elif data == 0x5E:
                    cnt = blob[p]; p += 1
                    for _ in range(cnt):
                        outline[row_base + col] = 255
                        col += 1
                elif data == 0x7E:
                    cnt = blob[p]; p += 1
                    col += cnt
                else:
                    raise SlpDecodeError('unsupported extended cmd 0x%02X' % data)
            else:
                raise SlpDecodeError('unknown cmd 0x%02X' % data)

    return indexes, alpha, player, shadow, outline


# --- PNG output (no third-party imaging dependency) --------------------------


def png_bytes(width, height, rgba):
    """Encode an 8-bit RGBA PNG. `rgba` is a flat bytes-like of width*height*4."""
    import zlib

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter type 0 (None)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack('>I', len(payload)) + tag + payload
                + struct.pack('>I', zlib.crc32(tag + payload) & 0xFFFFFFFF))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 6))
            + chunk(b'IEND', b''))


def write_png(path, width, height, rgba):
    """Write an 8-bit RGBA PNG."""
    with open(path, 'wb') as fh:
        fh.write(png_bytes(width, height, rgba))
