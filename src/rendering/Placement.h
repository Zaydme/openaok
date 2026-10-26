#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>

#include "ecs/Simulation.h"
#include "hal/GraphicCache.h"

namespace Rendering {

class Renderer;

// The building waiting to be placed, on the tile under the cursor:
//
// * In the world's own back-to-front order, not over it, so the building
//   being placed goes behind what stands in front of it, as the finished one
//   would.
// * Opaque and in its own colours, through the blitter's masks
//   (rendering/Stipple.h), as the placement check answers: one pixel in four
//   black where it can go; half its pixels a red that pulses with game time
//   (BlockedColour) where it cannot.
// * A wall line piece by piece, each checked on its own.
// * The Farm and the Fish Trap -- selection effect 1 -- with their outline on
//   the ground, light grey where they can go and red where they cannot.
// * No shadow.
namespace Placement {

struct Piece {
    const HAL::GraphicAtlas* atlas = nullptr;
    SDL_Texture* player = nullptr;   // the player's colour layer
    int frame = 0;
    float wx = 0.0f;                 // where its hotspot stands in the world
    float wy = 0.0f;
    bool clear = false;              // the placement check passes here
    // The building's own piece -- the single building's draw -- rather than
    // an annex or a wall segment: it pulses through another red.
    bool main = false;
};

struct Outline {
    bool shown = false;
    float wx = 0.0f;                 // the footprint's middle
    float wy = 0.0f;
    float rx = 0.0f;                 // the dat's outline radii, in tiles
    float ry = 0.0f;
    bool round = false;
    bool clear = false;
};

// The tile a building of `unit_id` (a build button's unit) is placed on for
// the point (wx, wy) under the cursor: the one that puts the building's middle
// nearest the point, centring the building being placed on the cursor.
// Tiles are centred on whole numbers, so an even footprint, whose
// middle is half a tile up and left of its origin tile, takes the tile half a
// tile down and right of the point.
void OriginTile(int unit_id, float wx, float wy, int& tile_x, int& tile_y);

// Palette indices of the outline.
constexpr int kOutlineClear = 0x85;
constexpr int kOutlineBlocked = 0x24;

// The pieces of the building being placed, in the order the finished
// building's would be drawn, and its outline; false, and nothing, when no
// building is being placed.
bool Collect(ECS::Simulation& sim, const Renderer& renderer, std::vector<Piece>& pieces,
             Outline& outline);

// The red (a palette index) a building that cannot go there pulses through,
// a step every 128 ms of game time. The building's own piece climbs
// 0x22..0x26 and back down; the annexes and a wall's pieces climb 0x24..0x27,
// then step to 0x28 -- one past the red ramp, a brown -- and back down
// 0x27..0x25.
int BlockedColour(uint64_t game_ms, bool main);

} // namespace Placement

} // namespace Rendering
