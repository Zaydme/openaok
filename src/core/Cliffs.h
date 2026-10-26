#pragma once
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/RMSParser.h"
#include "core/Random.h"
#include "physics/Grid.h"

// The cliff pass of a random map script, `<CLIFF_GENERATION>`, and the cliff
// pieces, which scenarios share.
//
// Cliffs are laid on a lattice of 3x3-tile cells. A cell is open to
// cliffs when its nine tiles are dry land of one elevation; water in a cell
// closes every cell within min_terrain_distance of it, each start closes the
// cells within 7 of its own (15 tiles / 3 + 2), and a cell with no open
// neighbour is closed too. A cliff is a walk from a random open cell, one cell
// a step into an open cell of the same elevation, `length` steps, turning a
// quarter now and then by its curliness; once it is laid, every cell within
// min_distance_cliffs of it is closed to the next.
//
// Each cell a cliff passes holds one piece, chosen by its links to the cells
// before and after it -- which of its four sides link, and for each link
// whether the cliff shows its face there (1) or only its lip (-1). The first
// link is a face, and every link after it takes the one value a piece exists
// for. The piece stands at the cell's corner plus the piece's own offset: a
// tile and a half both ways for the 3x3 pieces, half a tile off that for the
// smaller corner pieces. Neighbouring pieces are therefore exactly three tiles
// apart, where the art joins.
//
// All of that is in the original map frame (the dat's axes), whose x runs up
// the screen to the right and y down it to the right: a quarter turn from the
// grid's (the turn ecs/BuildingGeometry's FromDatAxes makes for the dat's
// offsets). The art joins only in that frame, so a cliff is laid out in it and
// each piece turned onto the grid.
namespace Core::Cliffs {

// One of the twenty cliff pieces. Links run +x, +y, -x, -y in the original
// map frame.
struct Piece {
    std::array<int8_t, 4> links;
    int16_t unit;       // the cliff unit (CLF01..CLF09) and frame of its graphic
    int8_t frame;
    int16_t alt_unit;   // a second drawing of a straight, taken half the time; -1: none
    int8_t alt_frame;
    int8_t x2;          // where it stands from the cell's corner, in half tiles
    int8_t y2;
};

// The piece for a cell's links, or null if there is none.
const Piece* PieceFor(const std::array<int8_t, 4>& links);

// The piece a cliff unit and frame draw, or null if it is none of them.
const Piece* PieceDrawn(int unit_id, int frame);

// A point of the grid in the original map frame, in half tiles (a tile t of
// that frame spanning [2t, 2t + 2)); and back. The frame's x is the grid's y
// turned round, its y the grid's x.
std::pair<int, int> OriginalHalves(const Physics::Grid& grid, Fixed32 x, Fixed32 y);
std::pair<Fixed32, Fixed32> FromOriginalHalves(const Physics::Grid& grid, int x2, int y2);

void Generate(Physics::Grid& grid, entt::registry& registry, const RMSSection& section,
              const std::vector<std::pair<int, int>>& starts, Random& rng);

// The tiles a cliff unit blocks, across and down the grid, by its dat size --
// for a scenario's cliffs too (ecs/ScenarioWorld.cpp).
void Footprint(int unit_id, int& ext_x, int& ext_y);

// Blocks the tiles a cliff unit standing at (x, y) covers.
void Block(Physics::Grid& grid, int unit_id, Fixed32 x, Fixed32 y);

} // namespace Core::Cliffs
