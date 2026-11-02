#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "rendering/TerrainRenderer.h"

namespace Verify {

// Every terrain set is a square tiling pattern, and the cell a tile draws is
// its terrain's own for the tile's place: in every block of a pattern's size,
// anywhere on the map, each of its frames is drawn once. So a tile whose
// terrain changes (a Farm planted on grass, ground painted in the editor)
// draws the new terrain's pattern, 3 by 3 for a Farm, where grass's is 10 by
// 10.
bool CheckTerrainCell() {
    const char* verdict = nullptr;
    static const int kFrameCounts[4] = {100, 64, 36, 9};
    static const int kPeriods[4] = {10, 8, 6, 3};
    for (int k = 0; k < 4 && verdict == nullptr; ++k) {
        const int frames = kFrameCounts[k], period = kPeriods[k];
        for (const int ox : {0, 1, 5, 37}) {
            for (const int oy : {0, 2, 7, 91}) {
                std::vector<int> seen(static_cast<size_t>(frames), 0);
                for (int y = oy; y < oy + period; ++y) {
                    for (int x = ox; x < ox + period; ++x) {
                        const int cell = Rendering::TerrainRenderer::PatternCell(x, y, frames);
                        if (cell < 0 || cell >= frames) {
                            verdict = "a tile draws a cell its terrain has not";
                        } else {
                            ++seen[static_cast<size_t>(cell)];
                        }
                    }
                }
                for (int c = 0; c < frames && verdict == nullptr; ++c) {
                    if (seen[static_cast<size_t>(c)] != 1) {
                        verdict = "a block of a terrain's pattern does not draw each of its cells once";
                    }
                }
            }
        }
    }
    std::printf("terraincell=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
