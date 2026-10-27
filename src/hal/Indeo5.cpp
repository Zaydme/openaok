// SPDX-License-Identifier: LGPL-2.1-or-later
// This file follows the Indeo Video 5 decoder of FFmpeg's libavcodec
// (indeo5.c, ivi.c, ivi_dsp.c, indeo5data.h),
// Copyright (c) 2009-2011 Maxim Poliakovski, and is licensed, as FFmpeg is,
// under the GNU Lesser General Public License 2.1 or later: see
// LICENSES/LGPL-2.1.txt.

#include "hal/Indeo5.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "hal/Indeo5Tables.h"

namespace HAL::Indeo5 {

namespace {

// The frame types a picture header names.
enum FrameType {
    kIntra = 0,
    kInter = 1,       // predicted, and predicted from in turn
    kInterScal = 2,   // the scalable mode's droppable kind
    kInterNoRef = 3,  // predicted, never predicted from
    kNull = 4,        // no data: the last picture again
};

// ---------------------------------------------------------------------------
// The bitstream: least significant bit first, so the next bits are the low
// end of the word read at the current byte.

uint64_t Window(const Bits& b) {
    const size_t byte = b.pos >> 3;
    uint64_t word = 0;
    const size_t have = byte < b.size ? std::min<size_t>(8, b.size - byte) : 0;
    for (size_t k = 0; k < have; ++k) word |= static_cast<uint64_t>(b.data[byte + k]) << (8 * k);
    return word >> (b.pos & 7);
}

// Up to 32 bits: the window holds at least 57 past the current bit.
uint32_t Peek(const Bits& b, int n) {
    return static_cast<uint32_t>(Window(b) & ((uint64_t{1} << n) - 1));
}

uint32_t Read(Bits& b, int n) {
    const uint32_t value = Peek(b, n);
    b.pos += static_cast<size_t>(n);
    return value;
}

bool Read1(Bits& b) { return Read(b, 1) != 0; }

void Skip(Bits& b, int n) { b.pos += static_cast<size_t>(n); }

void Align(Bits& b) { b.pos = (b.pos + 7) & ~size_t{7}; }

bool Overrun(const Bits& b) { return b.pos > b.size * 8; }

// A symbol of a codebook, or -1 where the bits start no code.
int ReadVlc(Bits& b, const VlcTable& table) {
    const uint16_t entry = table.entries[Peek(b, kVlcBits)];
    const int length = entry >> 8;
    if (length == 0) return -1;
    b.pos += static_cast<size_t>(length);
    return entry & 0xFF;
}

// The sign is the low bit: 0, -1, 1, -2, 2 ... for 0, 1, 2, 3, 4 ...
int ToSigned(int value) { return -((value >> 1) ^ -(value & 1)); }

// ---------------------------------------------------------------------------
// Codebooks.

// Fills a lookup from a codebook. A code is read first bit first as written
// most significant first: row i's i ones, its zero, then the index. Only 256
// symbols fit a byte, so a codebook describing more stops there.
bool BuildVlc(const Codebook& book, VlcTable& table) {
    table.entries.assign(size_t{1} << kVlcBits, 0);
    int symbol = 0;
    for (int row = 0; row < book.rows && symbol < 256; ++row) {
        const int xbits = book.xbits[row];
        const int not_last = row != book.rows - 1 ? 1 : 0;
        int length = row + xbits + not_last;
        if (length > kVlcBits) return false;
        for (int index = 0; index < (1 << xbits) && symbol < 256; ++index, ++symbol) {
            uint32_t pattern = (1u << row) - 1;   // the ones, read first
            int at = row + not_last;              // past the zero
            for (int bit = xbits - 1; bit >= 0; --bit, ++at) {
                if ((index >> bit) & 1) pattern |= 1u << at;
            }
            // A codebook of one symbol still spends a bit on it.
            const int bits = std::max(length, 1);
            for (uint32_t rest = 0; rest < (1u << (kVlcBits - bits)); ++rest) {
                table.entries[pattern | (rest << bits)] =
                    static_cast<uint16_t>((bits << 8) | symbol);
            }
        }
    }
    return true;
}

struct BuiltIn {
    VlcTable macroblock[8];
    VlcTable block[8];
};

const BuiltIn& BuiltInTables() {
    static const BuiltIn tables = [] {
        BuiltIn t;
        for (int i = 0; i < 8; ++i) {
            BuildVlc(Tables::kMacroblockBooks[i], t.macroblock[i]);
            BuildVlc(Tables::kBlockBooks[i], t.block[i]);
        }
        return t;
    }();
    return tables;
}

bool SameBook(const Codebook& a, const Codebook& b) {
    return a.rows == b.rows && std::memcmp(a.xbits, b.xbits, static_cast<size_t>(a.rows)) == 0;
}

// A header's choice of codebook: absent, the eighth built in; 0-6 the
// others; 7 a codebook spelt out, rebuilt only when it changes.
bool ReadHuffChoice(Bits& b, bool coded, bool for_blocks, HuffChoice& choice,
                    std::string& error) {
    const BuiltIn& builtin = BuiltInTables();
    const VlcTable* tables = for_blocks ? builtin.block : builtin.macroblock;
    if (!coded) {
        choice.table = &tables[7];
        return true;
    }
    const int selected = static_cast<int>(Read(b, 3));
    if (selected != 7) {
        choice.table = &tables[selected];
        return true;
    }
    Codebook book;
    book.rows = static_cast<int>(Read(b, 4));
    if (book.rows == 0) {
        error = "empty custom codebook";
        return false;
    }
    for (int i = 0; i < book.rows; ++i) book.xbits[i] = static_cast<uint8_t>(Read(b, 4));
    if (!choice.custom_built || !SameBook(book, choice.custom_book)) {
        choice.custom_book = book;
        choice.custom_built = BuildVlc(book, choice.custom);
        if (!choice.custom_built) {
            choice.custom_book.rows = 0;
            error = "invalid custom codebook";
            return false;
        }
    }
    choice.table = &choice.custom;
    return true;
}

// ---------------------------------------------------------------------------
// The layout.

int AlignUp(int value, int to) { return (value + to - 1) / to * to; }

void LayOutPlanes(Decoder& d, const Layout& layout) {
    for (int p = 0; p < 3; ++p) {
        Plane& plane = d.planes[p];
        plane.width = p == 0 ? layout.width : (layout.width + 3) >> 2;
        plane.height = p == 0 ? layout.height : (layout.height + 3) >> 2;
        Band& band = plane.band;
        band = Band{};
        band.plane = p;
        band.width = plane.width;
        band.height = plane.height;
        // Buffers aligned on the largest macroblock: 16 for luma, 8 chroma.
        const int align = p == 0 ? 16 : 8;
        band.pitch = AlignUp(band.width, align);
        band.aheight = AlignUp(band.height, align);
        for (auto& buffer : band.bufs) {
            buffer.assign(static_cast<size_t>(band.pitch) * static_cast<size_t>(band.aheight), 0);
        }
    }
}

bool LayOutTiles(Decoder& d, const Layout& layout, std::string& error) {
    for (int p = 0; p < 3; ++p) {
        Band& band = d.planes[p].band;
        const int tile_w = p == 0 ? layout.tile_width : (layout.tile_width + 3) >> 2;
        const int tile_h = p == 0 ? layout.tile_height : (layout.tile_height + 3) >> 2;
        if (tile_w <= 0 || tile_h <= 0) {
            error = "empty tile";
            return false;
        }
        band.tiles.clear();
        const std::vector<Tile>& luma = d.planes[0].band.tiles;
        size_t index = 0;
        for (int y = 0; y < band.height; y += tile_h) {
            for (int x = 0; x < band.width; x += tile_w, ++index) {
                Tile tile;
                tile.x = x;
                tile.y = y;
                tile.width = std::min(band.width - x, tile_w);
                tile.height = std::min(band.height - y, tile_h);
                tile.mb_size = band.mb_size;
                const int across = (tile.width + band.mb_size - 1) / band.mb_size;
                const int down = (tile.height + band.mb_size - 1) / band.mb_size;
                tile.mbs.resize(static_cast<size_t>(across * down));
                if (p != 0) {
                    // Chroma reads its motion and quantiser off luma's tile.
                    if (index >= luma.size() || luma[index].mbs.size() != tile.mbs.size()) {
                        error = "chroma tiles do not match luma";
                        return false;
                    }
                    tile.ref_mbs = luma[index].mbs.data();
                }
                band.tiles.push_back(std::move(tile));
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Headers.

// The group-of-pictures header every key frame carries: the picture's size,
// its tiling, and each band's block sizes, transform and quantiser tables.
bool ReadGopHeader(Decoder& d) {
    Bits& b = d.bits;
    d.gop_flags = static_cast<int>(Read(b, 8));
    if (d.gop_flags & 1) Skip(b, 16);   // the header's size
    if (d.gop_flags & 0x20) {
        d.error = "password-protected stream";
        return false;
    }
    const int tile_size = (d.gop_flags & 0x40) ? 64 << Read(b, 2) : 0;
    if (tile_size > 256) {
        d.error = "tile size out of range";
        return false;
    }
    const int luma_bands = static_cast<int>(Read(b, 2)) * 3 + 1;
    const int chroma_bands = static_cast<int>(Read(b, 1)) * 3 + 1;
    if (luma_bands != 1 || chroma_bands != 1) {
        d.error = "the scalable (wavelet) mode is not supported";
        return false;
    }
    Layout layout;
    const int size_index = static_cast<int>(Read(b, 4));
    if (size_index == 15) {
        layout.height = static_cast<int>(Read(b, 13));
        layout.width = static_cast<int>(Read(b, 13));
    } else {
        layout.height = Tables::kPictureSizes[size_index * 2 + 1] << 2;
        layout.width = Tables::kPictureSizes[size_index * 2] << 2;
    }
    if (layout.width <= 0 || layout.height <= 0 || layout.width > 4096 || layout.height > 4096) {
        d.error = "picture size out of range";
        return false;
    }
    if (d.gop_flags & 2) {
        d.error = "YV12 pictures are not supported";
        return false;
    }
    layout.tile_width = tile_size != 0 ? tile_size : layout.width;
    layout.tile_height = tile_size != 0 ? tile_size : layout.height;

    bool relayout = !d.configured || !(layout == d.layout) || d.gop_invalid;
    if (relayout) {
        LayOutPlanes(d, layout);
        d.layout = layout;
        d.configured = true;
    }

    // Each plane's band: luma first, then the chroma pair alike.
    for (int p = 0; p <= 1; ++p) {
        Band& band = d.planes[p].band;
        band.halfpel = Read1(b);
        const bool mb_is_block = Read1(b);
        const int blk_size = 8 >> Read(b, 1);
        const int mb_size = mb_is_block ? blk_size : blk_size * 2;
        if (p == 0 && blk_size == 4) {
            d.error = "4x4 luma blocks are not supported";
            return false;
        }
        if (mb_size != band.mb_size || blk_size != band.blk_size) {
            band.mb_size = mb_size;
            band.blk_size = blk_size;
            relayout = true;
        }
        if (Read1(b)) {
            d.error = "extended transform info is not supported";
            return false;
        }
        // Without the wavelet bands luma is a 2-D slant transform of 8x8
        // blocks in zigzag order, chroma one of 4x4 blocks.
        const int transform_size = p == 0 ? 8 : 4;
        if (band.blk_size != transform_size) {
            d.error = "block size does not match the transform";
            return false;
        }
        if (p == 0) {
            band.scan = Tables::kZigzag8x8;
            band.intra_base = Tables::kBaseQuant8x8Intra[0];
            band.inter_base = Tables::kBaseQuant8x8Inter[0];
            band.intra_scale = Tables::kScaleQuant8x8Intra[0];
            band.inter_scale = Tables::kScaleQuant8x8Inter[0];
        } else {
            band.scan = Tables::kDirectScan4x4;
            band.intra_base = Tables::kBaseQuant4x4Intra;
            band.inter_base = Tables::kBaseQuant4x4Inter;
            band.intra_scale = Tables::kScaleQuant4x4Intra;
            band.inter_scale = Tables::kScaleQuant4x4Inter;
        }
        if (Read(b, 2) != 0) {
            d.error = "band descriptor end marker missing";
            return false;
        }
    }
    // The second chroma plane is described by the first.
    {
        const Band& from = d.planes[1].band;
        Band& to = d.planes[2].band;
        if (to.mb_size != from.mb_size || to.blk_size != from.blk_size) relayout = true;
        to.mb_size = from.mb_size;
        to.blk_size = from.blk_size;
        to.halfpel = from.halfpel;
        to.scan = from.scan;
        to.intra_base = from.intra_base;
        to.inter_base = from.inter_base;
        to.intra_scale = from.intra_scale;
        to.inter_scale = from.inter_scale;
    }
    if (relayout && !LayOutTiles(d, layout, d.error)) return false;

    if (d.gop_flags & 8) {
        if (Read(b, 3) != 0) {
            d.error = "alignment bits are not zero";
            return false;
        }
        if (Read1(b)) Skip(b, 24);   // the transparency colour
    }
    Align(b);
    Skip(b, 23);   // meaning unknown
    if (Read1(b)) {
        // An extension, in 16-bit words while the top bit is set.
        while (Read(b, 16) & 0x8000) {
            if (Overrun(b)) {
                d.error = "truncated GOP extension";
                return false;
            }
        }
    }
    Align(b);
    return !Overrun(b);
}

void SkipExtension(Bits& b) {
    int length = 0;
    do {
        length = static_cast<int>(Read(b, 8));
        Skip(b, length * 8);
    } while (length != 0 && !Overrun(b));
}

bool ReadPictureHeader(Decoder& d) {
    Bits& b = d.bits;
    if (Read(b, 5) != 0x1F) {
        d.error = "no picture start code";
        return false;
    }
    d.prev_frame_type = d.frame_type;
    d.frame_type = static_cast<int>(Read(b, 3));
    if (d.frame_type >= 5) {
        d.frame_type = kIntra;
        d.error = "invalid frame type";
        return false;
    }
    d.frame_num = static_cast<int>(Read(b, 8));
    if (d.frame_type == kIntra) {
        if (!ReadGopHeader(d)) {
            d.gop_invalid = true;
            return false;
        }
        d.gop_invalid = false;
    }
    if (d.frame_type == kInterScal) {
        d.frame_type = kInter;
        d.error = "scalable inter frame in a stream that is not scalable";
        return false;
    }
    if (d.frame_type != kNull) {
        d.frame_flags = static_cast<int>(Read(b, 8));
        if (d.frame_flags & 1) Skip(b, 24);      // the header's size
        if (d.frame_flags & 0x10) Skip(b, 16);   // a checksum
        if (d.frame_flags & 0x20) SkipExtension(b);
        if (!ReadHuffChoice(b, (d.frame_flags & 0x40) != 0, false, d.mb_vlc, d.error)) return false;
        Skip(b, 3);   // meaning unknown
    }
    Align(b);
    return !Overrun(b);
}

bool ReadBandHeader(Decoder& d, Band& band) {
    Bits& b = d.bits;
    const int flags = static_cast<int>(Read(b, 8));
    if (flags & 1) {
        d.error = "empty band";
        return false;
    }
    if (d.frame_flags & 0x80) Skip(b, 24);   // the band's size
    band.inherit_mv = (flags & 2) != 0;
    band.inherit_qdelta = (flags & 8) != 0;
    band.qdelta_present = (flags & 4) != 0;
    if (!band.qdelta_present) band.inherit_qdelta = true;
    band.num_corr = 0;
    if (flags & 0x10) {
        band.num_corr = static_cast<int>(Read(b, 8));
        if (band.num_corr > 61) {
            d.error = "too many run/value corrections";
            return false;
        }
        for (int i = 0; i < band.num_corr * 2; ++i) band.corr[i] = static_cast<uint8_t>(Read(b, 8));
    }
    band.rvmap_sel = (flags & 0x40) ? static_cast<int>(Read(b, 3)) : 8;
    if (!ReadHuffChoice(b, (flags & 0x80) != 0, true, band.blk_vlc, d.error)) return false;
    if (Read1(b)) Skip(b, 16);   // a checksum
    band.glob_quant = static_cast<int>(Read(b, 5));
    if (flags & 0x20) {
        Align(b);
        SkipExtension(b);
    }
    Align(b);
    return !Overrun(b);
}

// ---------------------------------------------------------------------------
// The inverse slant transform, Indeo's integer stand-in for the DCT.

void Butterfly(int s1, int s2, int& o1, int& o2) {
    const int t = s1 - s2;
    o1 = s1 + s2;
    o2 = t;
}

// The reflection a, b = 1/2, 5/4.
void Reflect(int s1, int s2, int& o1, int& o2) {
    const int t = ((s1 + s2 * 2 + 2) >> 2) + s1;
    o2 = ((s1 * 2 - s2 + 2) >> 2) - s2;
    o1 = t;
}

// The reflection a, b = 1/2, 7/8.
void SlantPart4(int s1, int s2, int& o1, int& o2) {
    const int t = s2 + ((s1 * 4 - s2 + 4) >> 3);
    o2 = s1 + ((-s1 - s2 * 4 + 4) >> 3);
    o1 = t;
}

// One 8-point pass. `in` and `out` step by their own strides; `halve`
// rounds the second pass's results down by half.
template <typename In, typename Out>
void Slant8(const In* in, int in_step, Out* out, int out_step, bool halve) {
    const int s1 = in[0 * in_step], s4 = in[1 * in_step], s8 = in[2 * in_step],
              s5 = in[3 * in_step], s2 = in[4 * in_step], s6 = in[5 * in_step],
              s3 = in[6 * in_step], s7 = in[7 * in_step];
    int t1, t2, t3, t4, t5, t6, t7, t8;
    SlantPart4(s4, s5, t4, t5);
    Butterfly(s1, t5, t1, t5);
    Butterfly(s2, s6, t2, t6);
    Butterfly(s7, s3, t7, t3);
    Butterfly(t4, s8, t4, t8);
    Butterfly(t1, t2, t1, t2);
    Reflect(t4, t3, t4, t3);
    Butterfly(t5, t6, t5, t6);
    Reflect(t8, t7, t8, t7);
    Butterfly(t1, t4, t1, t4);
    Butterfly(t2, t3, t2, t3);
    Butterfly(t5, t8, t5, t8);
    Butterfly(t6, t7, t6, t7);
    const int r[8] = {t1, t2, t3, t4, t5, t6, t7, t8};
    for (int i = 0; i < 8; ++i) {
        out[i * out_step] = static_cast<Out>(halve ? (r[i] + 1) >> 1 : r[i]);
    }
}

template <typename In, typename Out>
void Slant4(const In* in, int in_step, Out* out, int out_step, bool halve) {
    const int s1 = in[0 * in_step], s4 = in[1 * in_step], s2 = in[2 * in_step],
              s3 = in[3 * in_step];
    int t1, t2, t3, t4;
    Butterfly(s1, s2, t1, t2);
    Reflect(s4, s3, t4, t3);
    Butterfly(t1, t4, t1, t4);
    Butterfly(t2, t3, t2, t3);
    const int r[4] = {t1, t2, t3, t4};
    for (int i = 0; i < 4; ++i) {
        out[i * out_step] = static_cast<Out>(halve ? (r[i] + 1) >> 1 : r[i]);
    }
}

// Columns, then rows. A column with no coefficient (flags) and a row of
// zeros come out zero, so are skipped.
void InverseSlant8x8(const int32_t* in, int16_t* out, int pitch, const uint8_t* flags) {
    int32_t tmp[64];
    for (int i = 0; i < 8; ++i) {
        if (flags[i]) {
            Slant8(in + i, 8, tmp + i, 8, false);
        } else {
            for (int k = 0; k < 8; ++k) tmp[i + 8 * k] = 0;
        }
    }
    for (int row = 0; row < 8; ++row, out += pitch) {
        const int32_t* src = tmp + row * 8;
        bool any = false;
        for (int k = 0; k < 8; ++k) any |= src[k] != 0;
        if (any) {
            Slant8(src, 1, out, 1, true);
        } else {
            std::memset(out, 0, 8 * sizeof(out[0]));
        }
    }
}

void InverseSlant4x4(const int32_t* in, int16_t* out, int pitch, const uint8_t* flags) {
    int32_t tmp[16];
    for (int i = 0; i < 4; ++i) {
        if (flags[i]) {
            Slant4(in + i, 4, tmp + i, 4, false);
        } else {
            for (int k = 0; k < 4; ++k) tmp[i + 4 * k] = 0;
        }
    }
    for (int row = 0; row < 4; ++row, out += pitch) {
        const int32_t* src = tmp + row * 4;
        if (src[0] != 0 || src[1] != 0 || src[2] != 0 || src[3] != 0) {
            Slant4(src, 1, out, 1, true);
        } else {
            out[0] = out[1] = out[2] = out[3] = 0;
        }
    }
}

// A block with only its DC: every pixel that value, halved.
void DcSlant(int dc, int16_t* out, int pitch, int size) {
    const auto value = static_cast<int16_t>((dc + 1) >> 1);
    for (int y = 0; y < size; ++y, out += pitch) {
        for (int x = 0; x < size; ++x) out[x] = value;
    }
}

// ---------------------------------------------------------------------------
// Motion compensation: a block taken from the reference picture at a
// full- or half-pixel offset (`type` bit 0 horizontal, bit 1 vertical half),
// added to the block's residual (`add`) or standing alone.

void MotionCompensate(int16_t* buf, const int16_t* ref, int pitch, int size, int type, bool add) {
    for (int i = 0; i < size; ++i, buf += pitch, ref += pitch) {
        const int16_t* below = ref + pitch;
        for (int j = 0; j < size; ++j) {
            int value = ref[j];
            switch (type) {
            case 1: value = (ref[j] + ref[j + 1]) >> 1; break;
            case 2: value = (ref[j] + below[j]) >> 1; break;
            case 3: value = (ref[j] + ref[j + 1] + below[j] + below[j + 1]) >> 2; break;
            default: break;
            }
            buf[j] = static_cast<int16_t>(add ? buf[j] + value : value);
        }
    }
}

bool Predict(Decoder& d, const Band& band, int offset, int mv_x, int mv_y, int type, bool add) {
    const int ref_offset = offset + mv_y * band.pitch + mv_x;
    const int buf_size = band.pitch * band.aheight;
    const int min_size = band.pitch * (band.blk_size - 1) + band.blk_size;
    const int ref_size = (type > 1 ? band.pitch : 0) + (type & 1);
    if (offset < 0 || ref_offset < 0 || buf_size - min_size < offset ||
        buf_size - min_size - ref_size < ref_offset) {
        d.error = "motion vector outside the reference";
        return false;
    }
    MotionCompensate(band.buf + offset, band.ref_buf + ref_offset, band.pitch, band.blk_size, type,
                     add);
    return true;
}

// ---------------------------------------------------------------------------
// Macroblocks and blocks.

int ScaleMv(int mv, int scale) { return (mv + (mv > 0 ? 1 : 0) + (scale - 1)) >> scale; }

// Each macroblock's type, coded blocks, quantiser and motion, for one tile.
bool ReadMacroblocks(Decoder& d, Band& band, Tile& tile) {
    Bits& b = d.bits;
    MacroBlock* mb = tile.mbs.data();
    const MacroBlock* ref_mb = tile.ref_mbs;
    if (ref_mb == nullptr && ((band.qdelta_present && band.inherit_qdelta) || band.inherit_mv)) {
        d.error = "inheritance without a reference band";
        return false;
    }
    const int mv_scale = (d.planes[0].band.mb_size >> 3) - (band.mb_size >> 3);
    const bool luma_quant = band.plane == 0 && (d.frame_flags & 8) != 0;
    const int buf_size = band.pitch * band.aheight;
    int mv_x = 0;
    int mv_y = 0;
    int row_offset = tile.y * band.pitch + tile.x;
    for (int y = tile.y; y < tile.y + tile.height; y += band.mb_size) {
        int offset = row_offset;
        for (int x = tile.x; x < tile.x + tile.width; x += band.mb_size) {
            mb->x = static_cast<int16_t>(x);
            mb->y = static_cast<int16_t>(y);
            mb->offset = static_cast<uint32_t>(offset);
            auto inherit_motion = [&] {
                if (mv_scale != 0) {
                    mb->mv_x = static_cast<int8_t>(ScaleMv(ref_mb->mv_x, mv_scale));
                    mb->mv_y = static_cast<int8_t>(ScaleMv(ref_mb->mv_y, mv_scale));
                } else {
                    mb->mv_x = ref_mb->mv_x;
                    mb->mv_y = ref_mb->mv_y;
                }
            };
            if (Read1(b)) {
                // An empty macroblock: always inter, nothing coded.
                if (d.frame_type == kIntra) {
                    d.error = "empty macroblock in a key frame";
                    return false;
                }
                mb->type = 1;
                mb->cbp = 0;
                mb->q_delta = 0;
                if (luma_quant) {
                    const int q = ReadVlc(b, *d.mb_vlc.table);
                    if (q < 0) { d.error = "bad macroblock code"; return false; }
                    mb->q_delta = static_cast<int8_t>(ToSigned(q));
                }
                mb->mv_x = mb->mv_y = 0;
                if (band.inherit_mv && ref_mb != nullptr) inherit_motion();
            } else {
                if (band.inherit_mv && ref_mb != nullptr) {
                    mb->type = ref_mb->type;
                } else if (d.frame_type == kIntra) {
                    mb->type = 0;
                } else {
                    mb->type = Read1(b) ? 1 : 0;
                }
                const int blocks = band.mb_size != band.blk_size ? 4 : 1;
                mb->cbp = static_cast<uint8_t>(Read(b, blocks));
                mb->q_delta = 0;
                if (band.qdelta_present) {
                    if (band.inherit_qdelta) {
                        if (ref_mb != nullptr) mb->q_delta = ref_mb->q_delta;
                    } else if (mb->cbp != 0 || luma_quant) {
                        const int q = ReadVlc(b, *d.mb_vlc.table);
                        if (q < 0) { d.error = "bad macroblock code"; return false; }
                        mb->q_delta = static_cast<int8_t>(ToSigned(q));
                    }
                }
                if (mb->type == 0) {
                    mb->mv_x = mb->mv_y = 0;
                } else if (band.inherit_mv && ref_mb != nullptr) {
                    inherit_motion();
                } else {
                    // Coded as differences from the previous macroblock's.
                    const int dy = ReadVlc(b, *d.mb_vlc.table);
                    const int dx = dy < 0 ? -1 : ReadVlc(b, *d.mb_vlc.table);
                    if (dx < 0) { d.error = "bad motion vector code"; return false; }
                    mv_y += ToSigned(dy);
                    mv_x += ToSigned(dx);
                    mb->mv_x = static_cast<int8_t>(mv_x);
                    mb->mv_y = static_cast<int8_t>(mv_y);
                }
            }
            if (mb->type != 0) {
                const int s = band.halfpel ? 1 : 0;
                if (x + (mb->mv_x >> s) + (y + (mb->mv_y >> s)) * band.pitch < 0 ||
                    x + ((mb->mv_x + s) >> s) + band.mb_size - 1 +
                            (y + band.mb_size - 1 + ((mb->mv_y + s) >> s)) * band.pitch >
                        buf_size - 1) {
                    d.error = "motion vector outside the reference";
                    return false;
                }
            }
            ++mb;
            if (ref_mb != nullptr) ++ref_mb;
            offset += band.mb_size;
        }
        row_offset += band.mb_size * band.pitch;
    }
    Align(b);
    return !Overrun(b);
}

// One coded block: run/value pairs, dequantised into place, transformed,
// and for an inter block added to its prediction.
bool DecodeCodedBlock(Decoder& d, const Band& band, const RunValueMap& rvmap, bool intra, int quant,
                      int offset, int mv_x, int mv_y, int mc_type, int& prev_dc) {
    Bits& b = d.bits;
    const uint16_t* base = intra ? band.intra_base : band.inter_base;
    const int blk_size = band.blk_size;
    const int num_coeffs = blk_size * blk_size;
    const int col_mask = blk_size - 1;
    if (band.pitch * (blk_size - 1) + blk_size > band.pitch * band.aheight - offset) {
        d.error = "block outside the band";
        return false;
    }
    int32_t coeffs[64] = {};
    uint8_t col_flags[8] = {};
    int scan_pos = -1;
    int sym = 0;
    const VlcTable& vlc = *band.blk_vlc.table;
    while (scan_pos <= num_coeffs) {
        sym = ReadVlc(b, vlc);
        if (sym < 0) {
            d.error = "bad block code";
            return false;
        }
        if (sym == rvmap.eob) break;
        int run = 0;
        int value = 0;
        if (sym == rvmap.esc) {
            // Escape: the run and the value's two halves, each its own code.
            const int r = ReadVlc(b, vlc);
            const int lo = r < 0 ? -1 : ReadVlc(b, vlc);
            const int hi = lo < 0 ? -1 : ReadVlc(b, vlc);
            if (hi < 0) {
                d.error = "bad escape code";
                return false;
            }
            run = r + 1;
            value = ToSigned((hi << 6) | lo);
        } else {
            run = rvmap.run[sym];
            value = rvmap.value[sym];
        }
        scan_pos += run;
        if (scan_pos >= num_coeffs || scan_pos < 0) break;
        const int pos = band.scan[scan_pos];
        const int q = (base[pos] * quant) >> 9;
        if (q > 1) value = value * q + (value > 0 ? 1 : -1) * (((q ^ 1) - 1) >> 1);
        coeffs[pos] = value;
        col_flags[pos & col_mask] |= value != 0 ? 1 : 0;
    }
    if (scan_pos < 0 || (scan_pos >= num_coeffs && sym != rvmap.eob)) {
        d.error = "corrupt block";
        return false;
    }
    // The DC of an intra block is coded as a difference from the last one.
    if (intra) {
        prev_dc += coeffs[0];
        coeffs[0] = prev_dc;
        col_flags[0] |= prev_dc != 0 ? 1 : 0;
    }
    if (blk_size == 8) {
        InverseSlant8x8(coeffs, band.buf + offset, band.pitch, col_flags);
    } else {
        InverseSlant4x4(coeffs, band.buf + offset, band.pitch, col_flags);
    }
    if (!intra) return Predict(d, band, offset, mv_x, mv_y, mc_type, true);
    return true;
}

bool DecodeBlocks(Decoder& d, const Band& band, const RunValueMap& rvmap, Tile& tile) {
    int prev_dc = 0;
    const int blk_size = band.blk_size;
    const int blocks = band.mb_size != blk_size ? 4 : 1;
    for (const MacroBlock& mb : tile.mbs) {
        const bool intra = mb.type == 0;
        uint32_t cbp = mb.cbp;
        int offset = static_cast<int>(mb.offset);
        const auto& scale = intra ? band.intra_scale : band.inter_scale;
        const int quant = scale[std::clamp(band.glob_quant + mb.q_delta, 0, 23)];
        int mv_x = 0;
        int mv_y = 0;
        int mc_type = 0;
        if (!intra) {
            mv_x = mb.mv_x;
            mv_y = mb.mv_y;
            if (band.halfpel) {
                mc_type = ((mv_y & 1) << 1) | (mv_x & 1);
                mv_x >>= 1;
                mv_y >>= 1;
            }
            const int cx = mb.mv_x & (band.halfpel ? 1 : 0);
            const int cy = mb.mv_y & (band.halfpel ? 1 : 0);
            if (mb.x + mv_x < 0 || mb.x + mv_x + band.mb_size + cx > band.pitch ||
                mb.y + mv_y < 0 || mb.y + mv_y + band.mb_size + cy > band.aheight) {
                d.error = "motion vector outside the band";
                return false;
            }
        }
        for (int blk = 0; blk < blocks; ++blk) {
            // The four blocks of a macroblock: top left, top right, bottom
            // left, bottom right.
            if (blk & 1) {
                offset += blk_size;
            } else if (blk == 2) {
                offset += blk_size * band.pitch - blk_size;
            }
            if (cbp & 1) {
                if (!DecodeCodedBlock(d, band, rvmap, intra, quant, offset, mv_x, mv_y, mc_type,
                                      prev_dc)) {
                    return false;
                }
            } else {
                if ((blk_size - 1) * band.pitch + blk_size > band.pitch * band.aheight - offset) {
                    d.error = "block outside the band";
                    return false;
                }
                // Not coded: an intra block is its running DC, an inter
                // block its prediction alone.
                if (intra) {
                    DcSlant(prev_dc, band.buf + offset, band.pitch, blk_size);
                } else if (!Predict(d, band, offset, mv_x, mv_y, mc_type, false)) {
                    return false;
                }
            }
            cbp >>= 1;
        }
    }
    Align(d.bits);
    return !Overrun(d.bits);
}

// A tile with no data: the reference picture's, moved by any motion the
// luma band hands down.
bool DecodeEmptyTile(Decoder& d, const Band& band, Tile& tile) {
    const int mv_scale = (d.planes[0].band.mb_size >> 3) - (band.mb_size >> 3);
    const bool clear_first = !band.qdelta_present && band.plane == 0;
    const int mb_size = band.mb_size;
    const int pitch = band.pitch;
    const MacroBlock* ref_mb = tile.ref_mbs;
    MacroBlock* mb = tile.mbs.data();
    bool need_mc = false;
    int row_offset = tile.y * pitch + tile.x;
    for (int y = tile.y; y < tile.y + tile.height; y += mb_size) {
        int offset = row_offset;
        for (int x = tile.x; x < tile.x + tile.width; x += mb_size) {
            mb->x = static_cast<int16_t>(x);
            mb->y = static_cast<int16_t>(y);
            mb->offset = static_cast<uint32_t>(offset);
            mb->type = 1;
            mb->cbp = 0;
            if (clear_first) {
                mb->q_delta = static_cast<int8_t>(band.glob_quant);
                mb->mv_x = mb->mv_y = 0;
            }
            if (ref_mb != nullptr) {
                if (band.inherit_qdelta) mb->q_delta = ref_mb->q_delta;
                if (band.inherit_mv) {
                    if (mv_scale != 0) {
                        mb->mv_x = static_cast<int8_t>(ScaleMv(ref_mb->mv_x, mv_scale));
                        mb->mv_y = static_cast<int8_t>(ScaleMv(ref_mb->mv_y, mv_scale));
                    } else {
                        mb->mv_x = ref_mb->mv_x;
                        mb->mv_y = ref_mb->mv_y;
                    }
                    need_mc |= mb->mv_x != 0 || mb->mv_y != 0;
                    const int h = band.halfpel ? 1 : 0;
                    const int dmv_x = mb->mv_x >> h;
                    const int dmv_y = mb->mv_y >> h;
                    if (mb->x + dmv_x < 0 || mb->x + dmv_x + mb_size + (mb->mv_x & h) > pitch ||
                        mb->y + dmv_y < 0 ||
                        mb->y + dmv_y + mb_size + (mb->mv_y & h) > band.aheight) {
                        d.error = "motion vector outside the band";
                        return false;
                    }
                }
                ++ref_mb;
            }
            ++mb;
            offset += mb_size;
        }
        row_offset += mb_size * pitch;
    }
    if (band.inherit_mv && need_mc) {
        const int blocks = mb_size != band.blk_size ? 4 : 1;
        for (const MacroBlock& m : tile.mbs) {
            int mv_x = m.mv_x;
            int mv_y = m.mv_y;
            int type = 0;
            if (band.halfpel) {
                type = ((mv_y & 1) << 1) | (mv_x & 1);
                mv_x >>= 1;
                mv_y >>= 1;
            }
            for (int blk = 0; blk < blocks; ++blk) {
                const int offset = static_cast<int>(m.offset) +
                                   band.blk_size * ((blk & 1) + ((blk & 2) ? pitch : 0));
                if (!Predict(d, band, offset, mv_x, mv_y, type, false)) return false;
            }
        }
    } else {
        const int16_t* src = band.ref_buf + tile.y * pitch + tile.x;
        int16_t* dst = band.buf + tile.y * pitch + tile.x;
        for (int y = 0; y < tile.height; ++y, src += pitch, dst += pitch) {
            std::memcpy(dst, src, static_cast<size_t>(tile.width) * sizeof(int16_t));
        }
    }
    return true;
}

// The run/value map's corrections: pairs of symbols swapped for this band,
// and swapped back after it.
void Correct(RunValueMap& map, const Band& band, bool undo) {
    for (int k = 0; k < band.num_corr; ++k) {
        const int i = undo ? band.num_corr - 1 - k : k;
        const uint8_t a = band.corr[i * 2];
        const uint8_t c = band.corr[i * 2 + 1];
        std::swap(map.run[a], map.run[c]);
        std::swap(map.value[a], map.value[c]);
        if (a == map.eob || c == map.eob) map.eob = static_cast<uint8_t>(map.eob ^ a ^ c);
        if (a == map.esc || c == map.esc) map.esc = static_cast<uint8_t>(map.esc ^ a ^ c);
    }
}

// The size of a tile's data, which counts its own header.
int ReadTileSize(Bits& b) {
    int length = 0;
    if (Read1(b)) {
        length = static_cast<int>(Read(b, 8));
        if (length == 255) length = static_cast<int>(Read(b, 24));
    }
    Align(b);
    return length;
}

bool DecodeBand(Decoder& d, Band& band) {
    band.buf = band.bufs[d.dst_buf].data();
    band.ref_buf = band.bufs[d.ref_buf].data();
    if (!ReadBandHeader(d, band)) return false;
    RunValueMap& rvmap = d.rvmaps[band.rvmap_sel];
    Correct(rvmap, band, false);
    Bits& b = d.bits;
    size_t pos = b.pos;
    bool ok = true;
    for (Tile& tile : band.tiles) {
        if (tile.mb_size != band.mb_size) {
            d.error = "macroblock size changed without a new layout";
            ok = false;
            break;
        }
        if (Read1(b)) {
            ok = DecodeEmptyTile(d, band, tile);
            if (!ok) break;
            continue;
        }
        const int size = ReadTileSize(b);
        if (size == 0) {
            d.error = "tile of no size";
            ok = false;
            break;
        }
        ok = ReadMacroblocks(d, band, tile) && DecodeBlocks(d, band, rvmap, tile);
        if (!ok) break;
        if (((b.pos - pos) >> 3) != static_cast<size_t>(size)) {
            d.error = "tile size mismatch";
            ok = false;
            break;
        }
        pos += static_cast<size_t>(size) << 3;
    }
    Correct(rvmap, band, true);
    Align(b);
    return ok;
}

// Which buffer this frame decodes into and which it predicts from.
void SwitchBuffers(Decoder& d) {
    if (d.prev_frame_type == kIntra || d.prev_frame_type == kInter) {
        d.buf_switch ^= 1;
        d.dst_buf = d.buf_switch;
        d.ref_buf = d.buf_switch ^ 1;
    }
    if (d.frame_type == kIntra) d.buf_switch = 0;
    if (d.frame_type == kIntra || d.frame_type == kInter) {
        d.dst_buf = d.buf_switch;
        d.ref_buf = d.buf_switch ^ 1;
    }
}

// A band's buffer, the bias of 128 the encoder took off given back.
void OutputPlane(const Plane& plane, std::vector<uint8_t>& dst) {
    const Band& band = plane.band;
    const int16_t* src = band.buf;
    uint8_t* out = dst.data();
    for (int y = 0; y < plane.height; ++y, out += plane.width, src += band.pitch) {
        for (int x = 0; x < plane.width; ++x) {
            out[x] = static_cast<uint8_t>(std::clamp(src[x] + 128, 0, 255));
        }
    }
}

} // namespace

void Init(Decoder& decoder) {
    decoder = Decoder{};
    for (int i = 0; i < 9; ++i) decoder.rvmaps[i] = Tables::kRunValueMaps[i];
}

Frame Decode(Decoder& d, const uint8_t* data, size_t size, Picture& out) {
    d.error.clear();
    if (size == 0) return Frame::Repeat;
    d.bits = Bits{data, size, 0};
    if (!ReadPictureHeader(d)) return Frame::Failed;
    if (d.gop_invalid) {
        d.error = "no key frame yet";
        return Frame::Failed;
    }
    SwitchBuffers(d);
    if (d.frame_type == kNull) {
        // Nothing to decode, and nothing new to show.
        return d.planes[0].band.buf != nullptr ? Frame::Repeat : Frame::Failed;
    }
    d.buf_invalid[d.dst_buf] = true;
    for (Plane& plane : d.planes) {
        if (!DecodeBand(d, plane.band)) return Frame::Failed;
    }
    d.buf_invalid[d.dst_buf] = false;

    const Plane& luma = d.planes[0];
    if (out.width != luma.width || out.height != luma.height) {
        out.width = luma.width;
        out.height = luma.height;
        out.chroma_width = d.planes[1].width;
        out.chroma_height = d.planes[1].height;
        out.y.assign(static_cast<size_t>(out.width * out.height), 0);
        out.u.assign(static_cast<size_t>(out.chroma_width * out.chroma_height), 128);
        out.v.assign(static_cast<size_t>(out.chroma_width * out.chroma_height), 128);
    }
    OutputPlane(d.planes[0], out.y);
    OutputPlane(d.planes[1], out.v);
    OutputPlane(d.planes[2], out.u);
    return Frame::Picture;
}

void ToArgb(const Picture& picture, uint8_t* pixels, int pitch) {
    // Fixed point out of 1 << 16: R = 1.164 (Y - 16) + 1.596 V',
    // G = 1.164 (Y - 16) - 0.813 V' - 0.391 U', B = 1.164 (Y - 16) + 2.018 U'.
    constexpr int kY = 76309, kRv = 104597, kGv = 53279, kGu = 25675, kBu = 132201;
    const auto clamp = [](int v) { return static_cast<uint32_t>(std::clamp(v >> 16, 0, 255)); };
    for (int y = 0; y < picture.height; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(pixels + static_cast<std::ptrdiff_t>(y) * pitch);
        const uint8_t* luma = picture.y.data() + static_cast<std::ptrdiff_t>(y) * picture.width;
        const int cy = std::min(y >> 2, picture.chroma_height - 1);
        const uint8_t* u = picture.u.data() + static_cast<std::ptrdiff_t>(cy) * picture.chroma_width;
        const uint8_t* v = picture.v.data() + static_cast<std::ptrdiff_t>(cy) * picture.chroma_width;
        for (int x = 0; x < picture.width; ++x) {
            const int cx = std::min(x >> 2, picture.chroma_width - 1);
            const int l = (luma[x] - 16) * kY + (1 << 15);
            const int cu = u[cx] - 128;
            const int cv = v[cx] - 128;
            row[x] = 0xFF000000u | clamp(l + kRv * cv) << 16 | clamp(l - kGv * cv - kGu * cu) << 8 |
                     clamp(l + kBu * cu);
        }
    }
}

} // namespace HAL::Indeo5
