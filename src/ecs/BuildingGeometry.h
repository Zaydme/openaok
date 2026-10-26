#pragma once
#include <functional>

#include "core/FixedPoint.h"
#include "ecs/components/Transform.h"

// Where a building sits on the ground: the one place a footprint's tiles and
// limits are worked out, for blocking the grid, freeing it, and telling a
// villager where the wall is.
namespace ECS::Buildings {

// The tile offsets a footprint covers, relative to its origin tile.
//
// An even footprint is anchored up and left: a two-tile building covers
// offsets -1 and 0.
struct TileSpan {
    int low = 0;
    int high = 0;
};

// A footprint is a rectangle: most buildings are square, a gate is four tiles
// along its wall and one across.
//
// The dat measures it on its own axes, which lie on the engine's turned a
// quarter: a piece the dat puts at (dx, dy) from a building's middle stands at
// (dy, -dx) here. So the dat's x extent is the engine's y extent.
struct Footprint {
    TileSpan x;
    TileSpan y;
};

// The ground a building occupies, in world units. Tiles are a unit across and
// centred on their integer coordinates, so this is the tile span widened by
// half a tile on every side.
struct Bounds {
    Core::Fixed32 min_x, min_y, max_x, max_y;

    // How far outside the building a point is; zero anywhere on or within it.
    Core::Fixed32 DistanceFrom(Components::Vector2D point) const;
};

// The single tile an object stands on -- a tree, a mine, a forage bush. The
// same rectangle as a footprint, so anything that approaches ground takes
// either without caring which it got.
Bounds TileBounds(Components::Vector2D origin);

// How many tiles across this unit's footprint is, from the dat's clearance:
// the longer side.
int FootprintTiles(int unit_id);

// The footprint's tile offsets on each axis.
Footprint FootprintOf(int unit_id);

// The x span alone, for callers that know the building is square.
TileSpan FootprintTileSpan(int unit_id);

// Where a piece the dat places at (dx, dy) from a building's middle lies on
// the engine's axes.
Components::Vector2D FromDatAxes(Core::Fixed32 dx, Core::Fixed32 dy);

// The limits of the ground this building stands on, given the tile it was
// placed on.
Bounds FootprintBounds(Components::Vector2D origin, int unit_id);

// Every tile the footprint covers.
void ForEachFootprintTile(int unit_id, int tile_x, int tile_y,
                          const std::function<void(int, int)>& visit);

} // namespace ECS::Buildings
