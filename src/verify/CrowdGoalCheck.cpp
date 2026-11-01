#include "verify/Harness.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "ecs/Simulation.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

// A route to a spot in the middle of a crowd standing there -- an army
// gathered where another is sent -- goes straight in: a search of a few
// hundred tiles, not one that spreads across the map looking for a way round
// a crowd there is no way round, and it ends at the spot.
bool CheckCrowdGoal() {
    ECS::Simulation sim;
    sim.Init();
    const auto& grid = sim.GetGrid();
    const char* verdict = nullptr;
    constexpr int kCrowd = 6;    // tiles each way from the spot
    constexpr int kAway = 16;    // tiles from the crowd the walk begins
    int ox = 0, oy = 0;
    for (int y = kCrowd + 2; y < grid.GetHeight() - kCrowd - 2 && ox == 0; ++y) {
        for (int x = kCrowd + 2; x < grid.GetWidth() - kCrowd - kAway - 2; ++x) {
            bool clear = true;
            for (int dy = -kCrowd - 1; dy <= kCrowd + 1 && clear; ++dy) {
                for (int dx = -kCrowd - 1; dx <= kCrowd + kAway + 1; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    int64_t searched = 0;
    bool reached = false;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        // Two bodies on every tile round the spot.
        const Core::Fixed32 tenth = Core::Fixed32(1) / Core::Fixed32(10);
        const Core::Fixed32 body = tenth * Core::Fixed32(2);
        std::vector<Pathfinding::Standing> standing;
        for (int dy = -kCrowd; dy <= kCrowd; ++dy) {
            for (int dx = -kCrowd; dx <= kCrowd; ++dx) {
                for (int side : {-1, 1}) {
                    const Core::Fixed32 x = Core::Fixed32(ox + dx) + tenth * Core::Fixed32(side * 2);
                    const Core::Fixed32 y(oy + dy);
                    standing.push_back({x - body, y - body, x + body, y + body});
                }
            }
        }
        const ECS::Components::Vector2D spot{Core::Fixed32(ox), Core::Fixed32(oy) + tenth * Core::Fixed32(4)};
        const ECS::Components::Vector2D from{Core::Fixed32(ox + kCrowd + kAway), Core::Fixed32(oy)};
        const int64_t before = Pathfinding::AStar::Searched();
        const auto route = Pathfinding::AStar::FindRoute(grid, from, spot, Physics::kLand, body, &standing);
        searched = Pathfinding::AStar::Searched() - before;
        reached = !route.waypoints.empty() && route.waypoints.back().x == spot.x && route.waypoints.back().y == spot.y;
        if (!reached) {
            verdict = "a route to a spot inside a crowd stopped short of it";
        } else if (searched > 1000) {
            verdict = "a route to a spot inside a crowd searched far and wide for a way round it";
        }
    }
    std::printf("crowdgoal=%s searched=%lld\n", verdict == nullptr ? "ok" : verdict, static_cast<long long>(searched));
    return verdict == nullptr;
}

} // namespace Verify
