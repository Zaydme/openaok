#include "verify/Harness.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kSide = 10;
constexpr int kUnits = kSide * kSide;

} // namespace

// A hundred soldiers each sent to the same place in one tick, standing in a
// block, share their routes: a few searches serve the block, rather than one
// each, every one going round the others still standing.
bool CheckSharedWalk() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = nullptr;
    int ox = 0, oy = 0;
    for (int y = 8; y < grid.GetHeight() - 8 && ox == 0; ++y) {
        for (int x = 2; x < grid.GetWidth() - 32; ++x) {
            bool clear = true;
            for (int dy = -7; dy <= 7 && clear; ++dy) {
                for (int dx = -1; dx <= 31; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    int64_t searched = 0;
    int routed = 0;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const int spear = cfg.GetUnitIdByName("SPRMN");
        std::vector<entt::entity> units;
        for (int i = 0; i < kUnits; ++i) {
            units.push_back(ECS::Units::Spawn(reg, spear, 1,
                                              {Core::Fixed32(ox + i % kSide), Core::Fixed32(oy - kSide / 2 + i / kSide)}));
        }
        sim.Update(16);
        for (auto e : units) {
            Core::CommandQueue::Get().PushCommand(Core::MoveCommand{e, Core::Fixed32(ox + 29), Core::Fixed32(oy + 3), 1});
        }
        const int64_t before = Pathfinding::AStar::Searched();
        sim.Update(16);
        searched = Pathfinding::AStar::Searched() - before;
        for (auto e : units) {
            const auto* path = reg.try_get<C::Path>(e);
            if (path != nullptr && path->current_waypoint_index < path->waypoints.size()) ++routed;
        }
        if (routed < kUnits) {
            verdict = "soldiers sent to one place together did not all set off";
        } else if (searched > 1500) {
            verdict = "soldiers sent to one place together each searched for a route of their own";
        }
    }
    std::printf("sharedwalk=%s routed=%d/%d searched=%lld\n", verdict == nullptr ? "ok" : verdict, routed, kUnits,
                static_cast<long long>(searched));
    return verdict == nullptr;
}

} // namespace Verify
