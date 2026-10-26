#pragma once
#include "physics/Grid.h"
#include "ecs/components/Path.h"

namespace Pathfinding {

// Every question here is asked for one passability layer (physics/Grid): land
// unless said otherwise, the sea for a ship. A route, a region and a clear line
// are each the layer's own.

// Which connected stretch of passable ground a tile is in, -1 for a tile none
// of the layer's movers can stand on. Two tiles in different regions have no
// route between them.
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
};

} // namespace Pathfinding
