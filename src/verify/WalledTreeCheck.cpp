#include "verify/Harness.h"

#include <cstdio>
#include <utility>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kTree = 411;
constexpr int kVillager = 83;

} // namespace

// A lumberjack whose tree runs out goes on to the nearest tree it can come
// up to: not one walled in on every side, open only at a corner, though it
// is nearer, but the next beyond it.
bool CheckWalledTree() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const char* verdict = nullptr;
    int ox = 0, oy = 0;
    for (int y = 8; y < grid.GetHeight() - 8 && ox == 0; ++y) {
        for (int x = 8; x < grid.GetWidth() - 8; ++x) {
            bool clear = true;
            for (int dy = -6; dy <= 6 && clear; ++dy) {
                for (int dx = -6; dx <= 6; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    bool took_open = false;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        auto place = [&](int x, int y) {
            return ECS::ScenarioWorld::Place(reg, grid, kTree, Core::PLAYER_GAIA, {Core::Fixed32(x), Core::Fixed32(y)}, {}, 0);
        };
        const entt::entity first = place(ox, oy);
        place(ox + 1, oy - 1);   // walled in below
        for (const auto& [x, y] : {std::pair{ox, oy - 1}, {ox + 2, oy - 1}, {ox + 1, oy - 2}, {ox + 1, oy}}) {
            grid.SetWalkable(x, y, false);
        }
        const entt::entity open = place(ox - 2, oy + 3);
        if (first == entt::null || open == entt::null) {
            verdict = "no trees placed";
        } else {
            reg.get<C::Resource>(first).current_amount = 5;
            const entt::entity villager = ECS::Units::Spawn(reg, kVillager, 1, {Core::Fixed32(ox - 1), Core::Fixed32(oy)});
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{villager, first, 1});
            for (int t = 0; t < 40 * kTicksPerSecond && !took_open; ++t) {
                sim.Update(16);
                took_open = reg.get<C::Worker>(villager).target_resource == open;
            }
            if (!took_open) verdict = "a lumberjack went for a tree walled in on every side, not the open one beyond";
        }
    }
    std::printf("walledtree=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
