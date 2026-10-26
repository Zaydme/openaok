// SPDX-License-Identifier: LGPL-2.1-or-later
// This file follows the Indeo Video 5 decoder of FFmpeg's libavcodec
// (indeo5.c, ivi.c, ivi_dsp.c, indeo5data.h),
// Copyright (c) 2009-2011 Maxim Poliakovski, and is licensed, as FFmpeg is,
// under the GNU Lesser General Public License 2.1 or later: see
// LICENSES/LGPL-2.1.txt.

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Intel Indeo Video Interactive 5 (fourcc IV50): the codec of the three
// movies in GAME/AVI, the Microsoft and Ensemble logos and the intro.
//
// The bitstream as FFmpeg's libavcodec documents it (indeo5.c, ivi.c,
// ivi_dsp.c), as plain data and free functions. The game's files are 320x240
// YVU9, one band per plane, one tile, every frame a key frame; inter frames
// and tiles are decoded too, as the format defines them. The scalable
// (wavelet) mode, with four luma bands, is refused.
//
// Pictures are YVU9: luma at full size, each chroma plane a quarter of it
// each way. Nothing is allocated per frame once the first key frame has set
// the layout up.
namespace HAL::Indeo5 {

// The longest code any of the format's Huffman codebooks may have.
constexpr int kVlcBits = 13;

// A Huffman codebook as the stream describes it: rows of codes, row i being
// i one-bits, a zero bit (the last row has none) and then xbits[i] bits of
// index. The symbol is the code's position counting from the first row.
struct Codebook {
    int rows = 0;
    uint8_t xbits[16] = {};
};

// One codebook's lookup: the next kVlcBits bits of the stream, first bit
// lowest, index an entry of (length << 8) | symbol, or 0 where no code
// starts with those bits.
struct VlcTable {
    std::vector<uint16_t> entries;
};

// The table a picture or band header chose: one of the eight built in, or a
// codebook of its own, kept while later headers repeat it.
struct HuffChoice {
    const VlcTable* table = nullptr;
    Codebook custom_book;
    bool custom_built = false;
    VlcTable custom;
};

// How a block's coefficients are coded: each symbol a run of zeros and the
// value that ends it, with one symbol for end-of-block and one escaping to a
// run and value spelt out.
struct RunValueMap {
    uint8_t eob = 0;
    uint8_t esc = 0;
    uint8_t run[256] = {};
    int8_t value[256] = {};
};

struct MacroBlock {
    int16_t x = 0;
    int16_t y = 0;
    uint32_t offset = 0;   // of its top-left pixel in the band's buffer
    uint8_t type = 0;      // 0 intra, 1 inter
    uint8_t cbp = 0;       // which of its blocks are coded
    int8_t q_delta = 0;
    int8_t mv_x = 0;
    int8_t mv_y = 0;
};

struct Tile {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int mb_size = 0;
    std::vector<MacroBlock> mbs;
    // The luma tile's macroblocks, which a chroma tile inherits its motion
    // and quantiser from; null for the luma band itself.
    const MacroBlock* ref_mbs = nullptr;
};

// One plane's picture data and how it is coded. The format allows several
// wavelet bands per plane; without the scalable mode each plane is one.
struct Band {
    int plane = 0;
    int width = 0;
    int height = 0;
    int pitch = 0;     // the buffers' row length, aligned to the macroblock
    int aheight = 0;   // and their height
    int mb_size = 0;
    int blk_size = 0;
    bool halfpel = false;
    bool inherit_mv = false;
    bool inherit_qdelta = false;
    bool qdelta_present = false;
    int glob_quant = 0;
    int num_corr = 0;
    uint8_t corr[61 * 2] = {};
    int rvmap_sel = 8;
    HuffChoice blk_vlc;
    const uint16_t* intra_base = nullptr;
    const uint16_t* inter_base = nullptr;
    const uint8_t* intra_scale = nullptr;
    const uint8_t* inter_scale = nullptr;
    const uint8_t* scan = nullptr;
    // Two pictures, the one being decoded and the one it predicts from;
    // which is which alternates.
    std::vector<int16_t> bufs[2];
    int16_t* buf = nullptr;
    const int16_t* ref_buf = nullptr;
    std::vector<Tile> tiles;
};

struct Plane {
    int width = 0;
    int height = 0;
    Band band;
};

// The layout a key frame's group-of-pictures header sets.
struct Layout {
    int width = 0;
    int height = 0;
    int tile_width = 0;
    int tile_height = 0;
    bool operator==(const Layout&) const = default;
};

// A bit reader over one frame, least significant bit of each byte first.
struct Bits {
    const uint8_t* data = nullptr;
    size_t size = 0;   // bytes
    size_t pos = 0;    // bits
};

struct Decoder {
    Plane planes[3];   // Y, V, U -- YVU9's order
    Layout layout;
    bool configured = false;
    RunValueMap rvmaps[9];
    HuffChoice mb_vlc;
    Bits bits;
    int frame_type = 0;
    int prev_frame_type = 0;
    int frame_num = 0;
    int frame_flags = 0;
    int gop_flags = 0;
    bool gop_invalid = true;
    int buf_switch = 0;
    int dst_buf = 0;
    int ref_buf = 1;
    bool buf_invalid[2] = {false, false};
    // Why the last frame failed.
    std::string error;
};

// A decoded picture, planar.
struct Picture {
    int width = 0;
    int height = 0;
    int chroma_width = 0;
    int chroma_height = 0;
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
};

enum class Frame {
    Picture,   // a new picture is in the Picture given
    Repeat,    // a null frame: the last picture stands
    Failed,    // Decoder::error says why
};

// Readies a decoder for a stream; the AVI's size is only a first guess, the
// key frames' headers have the last word.
void Init(Decoder& decoder);

// Decodes one compressed frame. An empty frame (a zero-length AVI chunk) is
// a Repeat.
Frame Decode(Decoder& decoder, const uint8_t* data, size_t size, Picture& out);

// The picture as 32-bit 0xAARRGGBB pixels (SDL's ARGB8888), `pitch` bytes a
// row: ITU-R BT.601 at studio range, each chroma sample spread over the 4x4
// luma pixels it stands for.
void ToArgb(const Picture& picture, uint8_t* pixels, int pitch);

} // namespace HAL::Indeo5
