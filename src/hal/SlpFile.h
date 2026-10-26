#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Reader for the DRS archives and the SLP sprites inside them. A decoded frame
// keeps its shadow, player-colour and outline pixels apart, and carries its
// hotspot.
namespace HAL {

// One file inside a DRS archive.
struct DrsEntry {
    uint32_t offset = 0;
    uint32_t size = 0;
};

// A whole archive held in memory (graphics.drs is about 70 MB).
struct DrsArchive {
    std::vector<uint8_t> data;
    std::unordered_map<uint32_t, DrsEntry> entries;
};

// Per-frame header exactly as stored: width/height plus the hotspot, which is
// the texel that sits on the object's tile.
struct SlpFrameHeader {
    uint32_t cmd_table_offset = 0;
    uint32_t outline_table_offset = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t hotspot_x = 0;
    int32_t hotspot_y = 0;
};

// A decoded frame. The four masks are parallel to `indexes`, one byte per
// pixel: `alpha` marks drawn pixels, `player` carries the palette index of a
// player-colour pixel so its shade survives, `shadow` and `outline` are flags.
struct SlpFrame {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t hotspot_x = 0;
    int32_t hotspot_y = 0;
    std::vector<uint8_t> indexes;
    std::vector<uint8_t> alpha;
    std::vector<uint8_t> player;
    std::vector<uint8_t> shadow;
    std::vector<uint8_t> outline;
};

struct PaletteColor {
    uint8_t r = 0, g = 0, b = 0;
};

// Reads a DRS archive into memory. Returns false if the file is missing or the
// header does not parse.
bool DrsOpen(const std::string& path, DrsArchive& out);

// Raw bytes of one archive entry, as text. gamedata.drs holds the random map
// scripts and the AI scripts as plain `bina` text files.
bool DrsReadText(const DrsArchive& drs, uint32_t file_id, std::string& out);

// Frame headers for one SLP, without decoding pixels. Empty if absent.
std::vector<SlpFrameHeader> SlpReadHeaders(const DrsArchive& drs, uint32_t slp_id);

// Decodes one frame. Returns false if the SLP is absent, the frame index is out
// of range, or an unsupported draw command is hit.
bool SlpDecodeFrame(const DrsArchive& drs, uint32_t slp_id, uint32_t frame, SlpFrame& out);

// Reads a JASC-PAL palette file (the game's 50500 palette).
bool LoadJascPalette(const std::string& path, std::vector<PaletteColor>& out);

} // namespace HAL
