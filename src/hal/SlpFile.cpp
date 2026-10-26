#include "SlpFile.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace HAL {

namespace {

// Little-endian reads, through memcpy so there are no alignment assumptions.
uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

int32_t ReadI32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Pixel counts are encoded two ways depending on the command: either packed
// into the high nibble, or, when that nibble is zero, in the following byte.
uint32_t PixelCountFromData(const std::vector<uint8_t>& blob, uint8_t data, size_t& p) {
    const uint32_t packed = (data & 0xF0u) >> 4;
    if (packed != 0) return packed;
    return blob[p++];
}

} // namespace

bool DrsOpen(const std::string& path, DrsArchive& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[HAL] Cannot open DRS: " << path << std::endl;
        return false;
    }
    const std::streamsize size = file.tellg();
    // copyright[40] version[4] type[12] num_tables[4] first_file[4]
    if (size < 64) return false;

    out.data.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(out.data.data()), size)) return false;

    const uint8_t* base = out.data.data();
    const uint32_t num_tables = ReadU32(base + 56);
    if (num_tables > 64) return false; // Implausible; treat as a bad header.

    for (uint32_t t = 0; t < num_tables; ++t) {
        const size_t rec = 64 + static_cast<size_t>(t) * 12;
        if (rec + 12 > out.data.size()) return false;
        const uint32_t table_offset = ReadU32(base + rec + 4);
        const uint32_t num_files = ReadU32(base + rec + 8);

        for (uint32_t i = 0; i < num_files; ++i) {
            const size_t e = table_offset + static_cast<size_t>(i) * 12;
            if (e + 12 > out.data.size()) return false;
            DrsEntry entry;
            const uint32_t file_id = ReadU32(base + e);
            entry.offset = ReadU32(base + e + 4);
            entry.size = ReadU32(base + e + 8);
            if (entry.offset + entry.size <= out.data.size()) {
                out.entries.emplace(file_id, entry);
            }
        }
    }
    return true;
}

bool DrsReadText(const DrsArchive& drs, uint32_t file_id, std::string& out) {
    auto it = drs.entries.find(file_id);
    if (it == drs.entries.end()) return false;
    const DrsEntry& entry = it->second;
    if (static_cast<size_t>(entry.offset) + entry.size > drs.data.size()) return false;
    out.assign(reinterpret_cast<const char*>(drs.data.data() + entry.offset), entry.size);
    return true;
}

std::vector<SlpFrameHeader> SlpReadHeaders(const DrsArchive& drs, uint32_t slp_id) {
    std::vector<SlpFrameHeader> headers;
    auto it = drs.entries.find(slp_id);
    if (it == drs.entries.end()) return headers;

    const uint8_t* base = drs.data.data() + it->second.offset;
    if (it->second.size < 32) return headers;

    // version[4] num_frames[2] properties[2] comment[24]
    const char version0 = static_cast<char>(base[0]);
    const char version3 = static_cast<char>(base[3]);
    if (version3 == 'P' || version0 == '4') {
        return headers; // Compressed or 32-bit variants; not in the game's data.
    }

    const uint16_t num_frames = ReadU16(base + 4);
    if (32u + static_cast<uint32_t>(num_frames) * 32u > it->second.size) return headers;

    headers.reserve(num_frames);
    for (uint16_t f = 0; f < num_frames; ++f) {
        const uint8_t* h = base + 32 + static_cast<size_t>(f) * 32;
        SlpFrameHeader hdr;
        hdr.cmd_table_offset = ReadU32(h);
        hdr.outline_table_offset = ReadU32(h + 4);
        hdr.width = ReadU32(h + 16);
        hdr.height = ReadU32(h + 20);
        hdr.hotspot_x = ReadI32(h + 24);
        hdr.hotspot_y = ReadI32(h + 28);
        headers.push_back(hdr);
    }
    return headers;
}

bool SlpDecodeFrame(const DrsArchive& drs, uint32_t slp_id, uint32_t frame, SlpFrame& out) {
    auto it = drs.entries.find(slp_id);
    if (it == drs.entries.end()) return false;

    const std::vector<SlpFrameHeader> headers = SlpReadHeaders(drs, slp_id);
    if (frame >= headers.size()) return false;
    const SlpFrameHeader& hdr = headers[frame];

    const size_t base = it->second.offset;
    const uint32_t w = hdr.width;
    const uint32_t h = hdr.height;

    out.width = w;
    out.height = h;
    out.hotspot_x = hdr.hotspot_x;
    out.hotspot_y = hdr.hotspot_y;

    const size_t n = static_cast<size_t>(w) * h;
    out.indexes.assign(n, 0);
    out.alpha.assign(n, 0);
    out.player.assign(n, 0);
    out.shadow.assign(n, 0);
    out.outline.assign(n, 0);
    if (n == 0) return true;

    const std::vector<uint8_t>& blob = drs.data;
    const size_t outline_table = base + hdr.outline_table_offset;
    const size_t cmd_table = base + hdr.cmd_table_offset;
    if (outline_table + h * 4 > blob.size() || cmd_table + h * 4 > blob.size()) return false;

    for (uint32_t row = 0; row < h; ++row) {
        // Outline table: the transparent run at each edge of this row.
        const uint16_t left = ReadU16(blob.data() + outline_table + row * 4);
        const uint16_t right = ReadU16(blob.data() + outline_table + row * 4 + 2);
        if (left == 0x8000 || right == 0x8000) continue; // Fully transparent row.

        size_t p = base + ReadU32(blob.data() + cmd_table + row * 4);
        uint32_t col = left;
        const size_t row_base = static_cast<size_t>(row) * w;

        // The guards below keep a malformed or truncated frame from writing
        // outside the buffer.
        while (p < blob.size()) {
            const uint8_t data = blob[p++];
            if (data == 0x0F) break;
            const uint8_t cmd = data & 0x0Fu;
            uint32_t count = 0;

            switch (cmd) {
            case 0x0: case 0x4: case 0x8: case 0xC: // Lesser block copy
                count = (data & 0xFCu) >> 2;
                for (uint32_t i = 0; i < count && col < w && p < blob.size(); ++i, ++col) {
                    out.indexes[row_base + col] = blob[p++];
                    out.alpha[row_base + col] = 255;
                }
                break;

            case 0x1: case 0x5: case 0x9: case 0xD: // Lesser skip
                col += (data & 0xFCu) >> 2;
                break;

            case 0x2: // Greater block copy
                if (p >= blob.size()) return false;
                count = ((data & 0xF0u) << 4) + blob[p++];
                for (uint32_t i = 0; i < count && col < w && p < blob.size(); ++i, ++col) {
                    out.indexes[row_base + col] = blob[p++];
                    out.alpha[row_base + col] = 255;
                }
                break;

            case 0x3: // Greater skip
                if (p >= blob.size()) return false;
                count = ((data & 0xF0u) << 4) + blob[p++];
                col += count;
                break;

            case 0x6: // Copy, player colour
                count = PixelCountFromData(blob, data, p);
                for (uint32_t i = 0; i < count && col < w && p < blob.size(); ++i, ++col) {
                    const uint8_t idx = blob[p++];
                    out.indexes[row_base + col] = idx;
                    out.alpha[row_base + col] = 255;
                    // 0 would read as "not player colour", so clamp it up.
                    out.player[row_base + col] = idx ? idx : 1;
                }
                break;

            case 0x7: { // Run of one colour
                count = PixelCountFromData(blob, data, p);
                if (p >= blob.size()) return false;
                const uint8_t idx = blob[p++];
                for (uint32_t i = 0; i < count && col < w; ++i, ++col) {
                    out.indexes[row_base + col] = idx;
                    out.alpha[row_base + col] = 255;
                }
                break;
            }

            case 0xA: { // Run, player colour
                count = PixelCountFromData(blob, data, p);
                if (p >= blob.size()) return false;
                const uint8_t idx = blob[p++];
                for (uint32_t i = 0; i < count && col < w; ++i, ++col) {
                    out.indexes[row_base + col] = idx;
                    out.alpha[row_base + col] = 255;
                    out.player[row_base + col] = idx ? idx : 1;
                }
                break;
            }

            case 0xB: // Shadow pixels
                count = PixelCountFromData(blob, data, p);
                for (uint32_t i = 0; i < count && col < w; ++i, ++col) {
                    out.shadow[row_base + col] = 255;
                }
                break;

            case 0xE: // Extended commands
                if (data == 0x4E) {            // Outline, one pixel
                    if (col < w) out.outline[row_base + col] = 255;
                    ++col;
                } else if (data == 0x6E) {     // Shield outline, one pixel
                    ++col;
                } else if (data == 0x5E) {     // Outline, run
                    if (p >= blob.size()) return false;
                    count = blob[p++];
                    for (uint32_t i = 0; i < count && col < w; ++i, ++col) {
                        out.outline[row_base + col] = 255;
                    }
                } else if (data == 0x7E) {     // Shield outline, run
                    if (p >= blob.size()) return false;
                    col += blob[p++];
                } else {
                    return false; // Obsolete or unimplemented (dither, alpha).
                }
                break;

            default:
                return false;
            }
        }
    }
    return true;
}

bool LoadJascPalette(const std::string& path, std::vector<PaletteColor>& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[HAL] Cannot open palette: " << path << std::endl;
        return false;
    }
    std::string magic, version, count_line;
    if (!std::getline(file, magic) || !std::getline(file, version)
        || !std::getline(file, count_line)) {
        return false;
    }
    int count = 0;
    try {
        count = std::stoi(count_line);
    } catch (...) { return false; }

    out.clear();
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string line;
        if (!std::getline(file, line)) break;
        std::istringstream ls(line);
        int r = 0, g = 0, b = 0;
        if (!(ls >> r >> g >> b)) break;
        out.push_back({static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)});
    }
    return !out.empty();
}

} // namespace HAL
