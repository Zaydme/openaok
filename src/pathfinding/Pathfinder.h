#pragma once
#include <vector>

#include "core/FixedPoint.h"
#include "physics/Grid.h"
#include "ecs/components/Path.h"

namespace Pathfinding {

// Every question here is asked for one passability layer (physics/Grid): land
// unless said otherwise, the sea for a ship. A route, a region and a clear line
// are each the layer's own.

// Which connected stretch of passable ground a tile is in, -1 for a tile none
// of the layer's movers can stand on. Two tiles in different regions have no
// route between them.
// Readies what the route search keeps of the map for every layer -- its
// terrain landmarks -- so that none is worked out in the middle of a busy
// tick: at once on a new map, then only when terrain changes what a layer may
// cross. Cheap when nothing has changed.
void Prepare(const Physics::Grid& grid);

int RegionAt(const Physics::Grid& grid, int x, int y,
             Physics::Layer layer = Physics::kLand);

// How many tiles a region holds: how big a body of water is, which computer
// players check before putting a dock on it.
int RegionSize(const Physics::Grid& grid, int region, Physics::Layer layer = Physics::kLand);

// Whether something standing on (x, y) -- a tree, a mine, a building's edge,
// or an open tile -- can be walked up to from `region`: the tile itself or
// one beside it is in that region.
bool Approachable(const Physics::Grid& grid, int region, int x, int y,
                  Physics::Layer layer = Physics::kLand);

// A unit standing still, as a route goes round it: its body's box, in world
// units.
struct Standing {
    Core::Fixed32 x0, y0, x1, y1;
};

class AStar {
public:
    // A deterministic tile path from start to end, with integer costs.
    static ECS::Components::Path FindPath(const Physics::Grid& grid, int start_x, int start_y,
                                          int end_x, int end_y,
                                          Physics::Layer layer = Physics::kLand);

    // The same route, but between two exact positions rather than two tiles.
    // The search runs over tiles; the final waypoint is replaced with the exact
    // destination. When the destination is in reach in a straight line, the
    // result is one waypoint and no search at all.
    static ECS::Components::Path FindPathTo(const Physics::Grid& grid,
                                            ECS::Components::Vector2D from,
                                            ECS::Components::Vector2D to,
                                            Physics::Layer layer = Physics::kLand);

    // A unit's route as it walks it, between two exact positions, for a body
    // reaching `radius` each way from its middle. Over the tiles, keeping off
    // the edges of closed ground, round the units `standing` where that
    // costs little and through them where it does not; then pulled into the
    // fewest straight lines the body can walk without touching closed ground
    // or going through anyone the search went round. A destination that
    // cannot be reached is come as near as the search gets. Empty when that
    // is no nearer.
    static ECS::Components::Path FindRoute(const Physics::Grid& grid,
                                           ECS::Components::Vector2D from,
                                           ECS::Components::Vector2D to, Physics::Layer layer,
                                           Core::Fixed32 radius,
                                           const std::vector<Standing>* standing = nullptr);

    // Whether a body reaching `radius` each way can go straight from a to b
    // without touching closed ground, but for what it touches already at
    // either end: a villager at a tree walks away from it.
    static bool BodyClear(const Physics::Grid& grid, ECS::Components::Vector2D a,
                          ECS::Components::Vector2D b, Physics::Layer layer, Core::Fixed32 radius);

    // Whether the line from a to b crosses no closed tile, exactly, but for
    // the one it starts on.
    static bool MiddleClear(const Physics::Grid& grid, ECS::Components::Vector2D a,
                            ECS::Components::Vector2D b, Physics::Layer layer);
};

} // namespace Pathfinding
