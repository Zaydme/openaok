#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Reader for the fog-of-war edge masks, Data\TileEdge.Dat and
// Data\BlkEdge.Dat (copies in assets/original/Data).
//
// Both files have the same layout, little endian:
//
//     uint32 set_offset[17]           one set per ground shape: flat, then the
//                                     16 slopes
//     at each set_offset:
//         uint32 mask_offset[count]   absolute file offsets
//     at each mask_offset:
//         { uint8 y, x0, x1 } ...     pixels x0..x1 inclusive of row y are set
//         uint8 255                   ends the mask
//
// A flat mask covers one 97x49 tile diamond (rows 0..48, columns 0..96);
// a slope's mask covers that slope's shape. TileEdge holds 94 masks a set,
// read as 47 pairs; BlkEdge 47.
namespace HAL {

struct EdgeSpan {
    uint8_t y = 0;
    uint8_t x0 = 0;
    uint8_t x1 = 0;
};

class EdgeMasks {
public:
    static constexpr int kSets = 17;

    // Reads `path`, expecting `count` masks in each of the 17 sets. False if
    // the file is missing or any offset falls outside it.
    bool Load(const std::string& path, int count);

    bool Loaded() const { return count_ > 0; }
    int Count() const { return count_; }
    const std::vector<EdgeSpan>& Mask(int set, int index) const {
        return masks_[static_cast<size_t>(set * count_ + index)];
    }

private:
    int count_ = 0;
    std::vector<std::vector<EdgeSpan>> masks_;   // set * count + index
};

} // namespace HAL
