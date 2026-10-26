#include "core/Cliffs.h"

#include <algorithm>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace Core::Cliffs {

namespace {

// Defaults for the figures a script leaves out.
constexpr int kDefaultMinCliffs = 3;
constexpr int kDefaultMaxCliffs = 9;
constexpr int kDefaultMinLength = 5;
constexpr int kDefaultMaxLength = 9;
constexpr int kDefaultCurliness = 36;
constexpr int kDefaultMinDistance = 2;
constexpr int kDefaultTerrainDistance = 2;
// The cells a start keeps clear: its land's 15 tiles, a third of that in
// cells, and two more.
constexpr int kStartClearCells = 15 / 3 + 2;
// A cliff shorter than this is not laid at all.
constexpr int kShortestCliff = 3;
// Draws follow the C library's rand() range, 0..32767.
constexpr int kRandMax = 32767;

// The cliff pieces. Offsets are in half tiles from the cell's corner; 3 is a
// tile and a half, the cell's middle.
constexpr Piece kPieces[] = {
    // Ends: one link.
    {{1, 0, 0, 0}, 264, 16, -1, -1, 3, 3},
    {{0, 1, 0, 0}, 264, 18, -1, -1, 3, 3},
    {{0, 0, 1, 0}, 264, 17, -1, -1, 3, 3},
    {{0, 0, 0, 1}, 264, 19, -1, -1, 3, 3},
    {{-1, 0, 0, 0}, 266, 20, -1, -1, 3, 3},
    {{0, -1, 0, 0}, 265, 22, -1, -1, 3, 3},
    {{0, 0, -1, 0}, 266, 21, -1, -1, 3, 3},
    {{0, 0, 0, -1}, 265, 23, -1, -1, 3, 3},
    // Corners: two links at a right angle.
    {{-1, 1, 0, 0}, 270, 0, -1, -1, 3, 4},
    {{0, -1, -1, 0}, 272, 9, -1, -1, 2, 4},
    {{0, 0, 1, -1}, 268, 6, -1, -1, 2, 3},
    {{1, 0, 0, 1}, 264, 3, -1, -1, 3, 3},
    {{1, -1, 0, 0}, 267, 12, -1, -1, 4, 3},
    {{0, 1, 1, 0}, 264, 15, -1, -1, 3, 3},
    {{0, 0, -1, 1}, 269, 14, -1, -1, 3, 2},
    {{-1, 0, 0, -1}, 271, 13, -1, -1, 4, 2},
    // Straights: two drawings each.
    {{1, 0, 1, 0}, 264, 4, 264, 5, 3, 3},
    {{0, 1, 0, 1}, 264, 1, 264, 2, 3, 3},
    {{-1, 0, -1, 0}, 266, 11, 266, 10, 3, 3},
    {{0, -1, 0, -1}, 265, 8, 265, 7, 3, 3},
};

// rand() * span / RAND_MAX, how every figure here is drawn: a pick in
// [0, span] whose top value comes up once in 32768.
int Draw(Random& rng, int span) {
    const int64_t r = rng.NextInt(kRandMax + 1);
    return static_cast<int>(r * span / kRandMax);
}

// The 3x3 cells, in the original map frame: 0 closed, else the elevation of
// the ground plus one.
struct Cells {
    int across = 0;
    int down = 0;
    std::vector<uint8_t> land;
    uint8_t& At(int cx, int cy) { return land[static_cast<size_t>(cy * across + cx)]; }
};

// Closes the cells within `d` of (cx, cy), a square.
void Close(Cells& cells, int cx, int cy, int d) {
    for (int y = std::max(0, cy - d); y <= std::min(cells.down - 1, cy + d); ++y) {
        for (int x = std::max(0, cx - d); x <= std::min(cells.across - 1, cx + d); ++x) {
            cells.At(x, y) = 0;
        }
    }
}

// One step of a walk: 0 is -y, 1 +x, 2 +y, 3 -x, into an open cell of the
// walk's elevation. A heading out of range is brought back, but not by a
// modulo: 4 is 1, 5 is 2, -1 is 2, -2 is 1.
bool Step(Cells& cells, int heading, int& cx, int& cy, uint8_t land) {
    if (heading > 3) heading -= 3 * ((heading - 1) / 3);
    if (heading < 0) heading += 3 * ((2 - heading) / 3);
    int x = cx;
    int y = cy;
    switch (heading) {
    case 0:
        if (y <= 0) return false;
        --y;
        break;
    case 1:
        if (x >= cells.across - 1) return false;
        ++x;
        break;
    case 2:
        if (y >= cells.down - 1) return false;
        ++y;
        break;
    case 3:
        if (x <= 0) return false;
        --x;
        break;
    default:
        return false;
    }
    if (cells.At(x, y) != land) return false;
    cx = x;
    cy = y;
    return true;
}

// Which side of cell a cell b is, as a link: +x, +y, -x, -y.
int LinkTowards(std::pair<int, int> a, std::pair<int, int> b) {
    if (b.first > a.first) return 0;
    if (b.first < a.first) return 2;
    return b.second > a.second ? 1 : 3;
}

// Whether a tile of the grid counts as water here: Water, Water 2, Water 3
// or the shallows.
bool Water(const Physics::Tile& tile) {
    const int dat = ConfigManager::Get().GetTerrainArt(tile.terrain).dat_id;
    return dat == 1 || dat == 22 || dat == 23 || dat == 4;
}

void Place(Physics::Grid& grid, entt::registry& registry, int cx, int cy, const Piece& piece,
           Random& rng) {
    int unit_id = piece.unit;
    int frame = piece.frame;
    // Half the time the other drawing of a straight.
    if (piece.alt_unit >= 0 && 2 * static_cast<int>(rng.NextInt(kRandMax + 1)) > kRandMax) {
        unit_id = piece.alt_unit;
        frame = piece.alt_frame;
    }
    const auto art = ConfigManager::Get().GetUnitGraphicById(unit_id, "idle_graphic0");
    if (!art.Valid()) return;
    const auto [x, y] = FromOriginalHalves(grid, 6 * cx + piece.x2, 6 * cy + piece.y2);
    Block(grid, unit_id, x, y);

    const entt::entity cliff = registry.create();
    registry.emplace<ECS::Components::Transform>(cliff, ECS::Components::Transform{{x, y}, {x, y}});
    registry.emplace<ECS::Components::Identity>(cliff, unit_id);
    registry.emplace<ECS::Components::Owner>(cliff, PLAYER_GAIA);
    ECS::Components::Sprite sprite{};
    sprite.slp_id = art.slp_id;
    sprite.slp_frame = frame;
    sprite.is_file_sprite = false;
    sprite.shadow_resolved = true;
    sprite.width = 0;
    sprite.height = 0;
    registry.emplace<ECS::Components::Sprite>(cliff, sprite);
}

} // namespace

const Piece* PieceFor(const std::array<int8_t, 4>& links) {
    for (const Piece& piece : kPieces) {
        if (piece.links == links) return &piece;
    }
    return nullptr;
}

const Piece* PieceDrawn(int unit_id, int frame) {
    for (const Piece& piece : kPieces) {
        if ((piece.unit == unit_id && piece.frame == frame) ||
            (piece.alt_unit == unit_id && piece.alt_frame == frame)) {
            return &piece;
        }
    }
    return nullptr;
}

// The grid's x is the original frame's y; its y the frame's x counted back
// from the far edge. A grid position stands half a tile back from the frame's
// (a tile t of the grid spans [t - 1/2, t + 1/2)).
std::pair<int, int> OriginalHalves(const Physics::Grid& grid, Fixed32 x, Fixed32 y) {
    const int x2 = 2 * grid.GetHeight() - (y * Fixed32(2)).ToInt() - 1;
    const int y2 = (x * Fixed32(2)).ToInt() + 1;
    return {x2, y2};
}

std::pair<Fixed32, Fixed32> FromOriginalHalves(const Physics::Grid& grid, int x2, int y2) {
    const Fixed32 half = Fixed32(1) / Fixed32(2);
    return {Fixed32(y2 - 1) * half, Fixed32(2 * grid.GetHeight() - x2 - 1) * half};
}

// The tiles a cliff unit blocks, by its dat size. The dat's x and y are the
// original frame's, a quarter turn from the grid's: a thin lip piece sized
// half a tile by one and a half lies along the grid's x.
void Footprint(int unit_id, int& ext_x, int& ext_y) {
    const Rules::UnitRules* rules = Rules::Unit(unit_id);
    ext_x = 3;
    ext_y = 3;
    if (rules == nullptr) return;
    ext_x = std::max(1, (rules->size_y * Fixed32(2)).ToInt());
    ext_y = std::max(1, (rules->size_x * Fixed32(2)).ToInt());
}

void Block(Physics::Grid& grid, int unit_id, Fixed32 x, Fixed32 y) {
    int ext_x = 3;
    int ext_y = 3;
    Footprint(unit_id, ext_x, ext_y);
    const Fixed32 half = Fixed32(1) / Fixed32(2);
    const int x0 = Physics::TileOf(x - Fixed32(ext_x) * half + half);
    const int y0 = Physics::TileOf(y - Fixed32(ext_y) * half + half);
    for (int ty = y0; ty < y0 + ext_y; ++ty) {
        for (int tx = x0; tx < x0 + ext_x; ++tx) {
            if (grid.InBounds(tx, ty)) grid.SetWalkable(tx, ty, false);
        }
    }
}

void Generate(Physics::Grid& grid, entt::registry& registry, const RMSSection& section,
              const std::vector<std::pair<int, int>>& starts, Random& rng) {
    int min_cliffs = kDefaultMinCliffs;
    int max_cliffs = kDefaultMaxCliffs;
    int min_length = kDefaultMinLength;
    int max_length = kDefaultMaxLength;
    int curliness = kDefaultCurliness;
    int min_distance = kDefaultMinDistance;
    int terrain_distance = kDefaultTerrainDistance;
    for (const RMSCommand& c : section.commands) {
        if (c.name == "min_number_of_cliffs") min_cliffs = c.Value(0, min_cliffs);
        else if (c.name == "max_number_of_cliffs") max_cliffs = c.Value(0, max_cliffs);
        else if (c.name == "min_length_of_cliff") min_length = c.Value(0, min_length);
        else if (c.name == "max_length_of_cliff") max_length = c.Value(0, max_length);
        else if (c.name == "cliff_curliness") curliness = c.Value(0, curliness);
        else if (c.name == "min_distance_cliffs") min_distance = c.Value(0, min_distance);
        else if (c.name == "min_terrain_distance") terrain_distance = c.Value(0, terrain_distance);
    }

    // The original map frame: its x runs along the grid's y.
    const int width = grid.GetHeight();
    const int height = grid.GetWidth();
    auto tile = [&](int ox, int oy) -> const Physics::Tile& {
        return grid.GetTile(oy, width - 1 - ox);
    };

    // The cells, a raster from the frame's corner. A cell of dry land at one
    // elevation opens; water closes the cells round it.
    Cells cells;
    cells.across = width / 3;
    cells.down = height / 3;
    cells.land.assign(static_cast<size_t>(cells.across * cells.down), 1);
    std::vector<std::pair<int, int>> open;
    for (int cy = 0; cy < cells.down; ++cy) {
        for (int cx = 0; cx < cells.across; ++cx) {
            bool even = true;
            bool wet = false;
            int elevation = -1;
            for (int j = 0; j < 3; ++j) {
                for (int i = 0; i < 3; ++i) {
                    const Physics::Tile& t = tile(3 * cx + i, 3 * cy + j);
                    if (Water(t)) {
                        wet = true;
                        even = false;
                    } else if (t.has_tree || t.blocked) {
                        // Woods are already planted at this point: a wood
                        // closes its cell, as uneven ground does.
                        even = false;
                    } else if (elevation < 0) {
                        elevation = t.elevation;
                    } else if (elevation != t.elevation) {
                        even = false;
                    }
                }
            }
            if (even) {
                if (cells.At(cx, cy) != 0) {
                    cells.At(cx, cy) = static_cast<uint8_t>(std::clamp(elevation, 0, 254) + 1);
                    open.emplace_back(cx, cy);
                }
            } else {
                if (wet) Close(cells, cx, cy, terrain_distance);
                cells.At(cx, cy) = 0;
            }
        }
    }
    for (const auto& [sx, sy] : starts) {
        // A start's tile in the original map frame.
        const int ox = width - 1 - sy;
        const int oy = sx;
        Close(cells, ox / 3, oy / 3, kStartClearCells);
    }
    // A cell no other cell touches is no place for a cliff.
    for (int cy = 0; cy < cells.down; ++cy) {
        for (int cx = 0; cx < cells.across; ++cx) {
            if (cells.At(cx, cy) == 0) continue;
            if (cy > 0 && cells.At(cx, cy - 1) != 0) continue;
            if (cx > 0 && cells.At(cx - 1, cy) != 0) continue;
            if (cy < cells.down - 1 && cells.At(cx, cy + 1) != 0) continue;
            if (cx < cells.across - 1 && cells.At(cx + 1, cy) != 0) continue;
            cells.At(cx, cy) = 0;
        }
    }
    // The open cells, shuffled; each cliff starts from the next still open.
    open.erase(std::remove_if(open.begin(), open.end(),
                              [&](const std::pair<int, int>& c) { return cells.At(c.first, c.second) == 0; }),
               open.end());
    for (size_t i = open.size(); i > 1; --i) {
        std::swap(open[i - 1], open[rng.NextInt(static_cast<uint32_t>(i))]);
    }

    // The count is the script's, whatever the map's size.
    const int wanted = min_cliffs + Draw(rng, max_cliffs - min_cliffs);
    for (int c = 0; c < wanted; ++c) {
        // One cliff.
        const int length = min_length + Draw(rng, max_length - min_length);
        if (open.empty() || length < kShortestCliff) continue;
        int x = -1;
        int y = -1;
        while (!open.empty()) {
            const auto [ox, oy] = open.back();
            open.pop_back();
            if (cells.At(ox, oy) != 0) {
                x = ox;
                y = oy;
                break;
            }
        }
        if (x < 0) break;
        const uint8_t land = cells.At(x, y);
        std::vector<std::pair<int, int>> walk{{x, y}};
        cells.At(x, y) = 0;
        // Straight on, with a chance at every step to turn a quarter one way
        // or the other. Where it cannot go on, the turns either side, and on
        // the first step even back -- the heading itself stays as it was.
        int heading = Draw(rng, 4);
        bool first = true;
        for (int n = 0; n < length; ++n) {
            const int r = Draw(rng, 100);
            if (r < curliness / 2) {
                heading = heading <= 0 ? 3 : heading - 1;
            } else if (r < curliness) {
                heading = heading >= 3 ? 0 : heading + 1;
            }
            const bool moved = Step(cells, heading, x, y, land) || Step(cells, heading + 1, x, y, land) ||
                               Step(cells, heading - 1, x, y, land) ||
                               (first && Step(cells, heading - 2, x, y, land));
            first = false;
            if (!moved) break;
            walk.emplace_back(x, y);
            cells.At(x, y) = 0;
        }

        // The cliff is drawn from where the walk ended back to where it began
        // (its cells come off a stack), each cell linked to the next. A link
        // shows its face if a piece has it so, and its lip otherwise.
        std::reverse(walk.begin(), walk.end());
        std::vector<std::array<int8_t, 4>> links(walk.size(), std::array<int8_t, 4>{0, 0, 0, 0});
        for (size_t i = 0; i + 1 < walk.size(); ++i) {
            const int side = LinkTowards(walk[i], walk[i + 1]);
            for (const int8_t value : {int8_t{1}, int8_t{-1}}) {
                std::array<int8_t, 4> trial = links[i];
                trial[static_cast<size_t>(side)] = value;
                if (PieceFor(trial) == nullptr) continue;
                links[i][static_cast<size_t>(side)] = value;
                links[i + 1][static_cast<size_t>((side + 2) % 4)] = value;
                break;
            }
        }
        for (size_t i = 0; i < walk.size(); ++i) {
            if (const Piece* piece = PieceFor(links[i])) {
                Place(grid, registry, walk[i].first, walk[i].second, *piece, rng);
            }
            Close(cells, walk[i].first, walk[i].second, min_distance);
        }
    }
}

} // namespace Core::Cliffs
