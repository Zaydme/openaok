#include "EdgeMasks.h"

#include <fstream>
#include <iterator>

namespace HAL {

namespace {

bool ReadU32(const std::vector<uint8_t>& data, size_t at, uint32_t& out) {
    if (at + 4 > data.size()) return false;
    out = static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) |
          (static_cast<uint32_t>(data[at + 2]) << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
    return true;
}

} // namespace

bool EdgeMasks::Load(const std::string& path, int count) {
    count_ = 0;
    masks_.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file || count <= 0) return false;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());

    std::vector<std::vector<EdgeSpan>> masks(static_cast<size_t>(kSets * count));
    for (int set = 0; set < kSets; ++set) {
        uint32_t table = 0;
        if (!ReadU32(data, static_cast<size_t>(set) * 4, table)) return false;
        for (int i = 0; i < count; ++i) {
            uint32_t at = 0;
            if (!ReadU32(data, table + static_cast<size_t>(i) * 4, at)) return false;
            if (at == 0) continue;   // a null entry is an empty mask
            auto& spans = masks[static_cast<size_t>(set * count + i)];
            size_t p = at;
            while (true) {
                if (p >= data.size()) return false;
                if (data[p] == 255) break;
                if (p + 3 > data.size()) return false;
                spans.push_back(EdgeSpan{data[p], data[p + 1], data[p + 2]});
                p += 3;
            }
        }
    }
    masks_ = std::move(masks);
    count_ = count;
    return true;
}

} // namespace HAL
