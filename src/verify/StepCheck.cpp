#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "ecs/Simulation.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

// A step within the tile a unit stands on, someone standing in the straight
// way, is still a way: the unit steps over, its body kept clear of theirs as
// it goes, rather than finding no route -- as a villager whose one open place
// at a tree is beside it, behind its crew mates, would otherwise give the
// tree up.
bool CheckStep() {
    ECS::Simulation sim;
    sim.Init();
    const auto& grid = sim.GetGrid();
    const char* verdict = nullptr;
    int ox = 0, oy = 0;
    for (int y = 4; y < grid.GetHeight() - 4 && ox == 0; ++y) {
        for (int x = 4; x < grid.GetWidth() - 4; ++x) {
            bool clear = true;
            for (int dy = -2; dy <= 2 && clear; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    size_t waypoints = 0;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        // From one corner of the tile to the other, a body standing in its
        // middle, clear of where the walk begins and ends.
        const Core::Fixed32 hundredth = Core::Fixed32(1) / Core::Fixed32(100);
        const Core::Fixed32 x(ox), y(oy);
        const Core::Fixed32 body = hundredth * Core::Fixed32(15);
        const Core::Fixed32 corner = hundredth * Core::Fixed32(45);
        const std::vector<Pathfinding::Standing> standing = {{x - body, y - body, x + body, y + body}};
        const auto route = Pathfinding::AStar::FindRoute(grid, {x + corner, y + corner}, {x - corner, y - corner},
                                                         Physics::kLand, hundredth * Core::Fixed32(10), &standing);
        waypoints = route.waypoints.size();
        if (route.waypoints.empty()) verdict = "no way across the tile a unit stands on, someone in the straight way";
    }
    std::printf("step=%s waypoints=%zu\n", verdict == nullptr ? "ok" : verdict, waypoints);
    return verdict == nullptr;
}

} // namespace Verify
