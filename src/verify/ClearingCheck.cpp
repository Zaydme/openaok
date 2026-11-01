#include "verify/Harness.h"

#include <cmath>
#include <cstdio>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// A soldier sent into a clearing walled in by trees, where no walk reaches,
// goes as near as its own ground allows -- to the trees' edge -- rather than
// standing where it was.
bool CheckClearing() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const char* verdict = nullptr;

    // Open ground twelve tiles every way, the clearing at its east end.
    int ox = 0, oy = 0;
    for (int y = 14; y < grid.GetHeight() - 14 && ox == 0; ++y) {
        for (int x = 14; x < grid.GetWidth() - 14; ++x) {
            bool clear = true;
            for (int dy = -12; dy <= 12 && clear; ++dy) {
                for (int dx = -12; dx <= 12; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    double moved = 0, off = 0;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const int px = ox + 8, py = oy;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) == 2 || std::abs(dy) == 2) grid.SetWalkable(px + dx, py + dy, false);
            }
        }
        const entt::entity spear = ECS::Units::Spawn(reg, Core::ConfigManager::Get().GetUnitIdByName("SPRMN"), 1,
                                                     {Core::Fixed32(ox - 8), Core::Fixed32(oy)});
        if (Pathfinding::RegionAt(grid, px, py, Physics::kLand) == Pathfinding::RegionAt(grid, ox - 8, oy, Physics::kLand)) {
            verdict = "the clearing is not walled in";
        } else {
            Core::CommandQueue::Get().PushCommand(Core::MoveCommand{spear, Core::Fixed32(px), Core::Fixed32(py), 1});
            for (int t = 0; t < 20 * kTicksPerSecond; ++t) sim.Update(16);
            const auto& at = reg.get<C::Transform>(spear).position;
            moved = std::hypot(at.x.ToFloat() - (ox - 8), at.y.ToFloat() - oy);
            off = std::hypot(at.x.ToFloat() - px, at.y.ToFloat() - py);
            if (moved < 1.0) verdict = "a soldier sent into a clearing walled in by trees stood where it was";
            else if (off > 4.0) verdict = "a soldier sent into a clearing walled in by trees stopped short of its edge";
        }
    }
    std::printf("clearing=%s moved=%.1f off=%.1f\n", verdict == nullptr ? "ok" : verdict, moved, off);
    return verdict == nullptr;
}

} // namespace Verify
