#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "ecs/Buildings.h"
#include "ecs/Simulation.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
using Core::Fixed32;
constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;
constexpr int kTicksPerSecond = 1000 / 16;
// The palisade block's reach from its middle, and how far from the start it
// is looked for, in tiles.
constexpr int kBlock = 3;
constexpr int kFrom = 10;

} // namespace

// A villager with a load of wood in the middle of a block of palisade, on
// ground of no region, where no route leads out: every drop site seems near
// and none can be reached. It stands with its load rather than look for a
// drop site every tick.
bool CheckDropoffBeyond() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);

    entt::entity villager = entt::null;
    auto owned = reg.view<C::Worker, C::Owner>();
    for (auto e : owned) {
        if (villager == entt::null && owned.get<C::Owner>(e).player_id == kUs) villager = e;
    }
    // Open ground for the block, towards the map's middle.
    const auto [hx, hy] = sim.player_starts[0];
    const int sx = grid.GetWidth() / 2 > hx ? 1 : -1, sy = grid.GetHeight() / 2 > hy ? 1 : -1;
    int cx = -1, cy = -1;
    for (int r = kFrom; r < kFrom + 20 && cx < 0; ++r) {
        for (int d = 0; d <= r && cx < 0; ++d) {
            const int x = hx + sx * r, y = hy + sy * d;
            bool open = true;
            for (int oy = -kBlock; oy <= kBlock && open; ++oy) {
                for (int ox = -kBlock; ox <= kBlock && open; ++ox) open = grid.IsWalkable(x + ox, y + oy);
            }
            if (open) {
                cx = x;
                cy = y;
            }
        }
    }
    if (villager == entt::null) {
        verdict = "no villager";
    } else if (cx < 0) {
        verdict = "no open ground for the palisade";
    } else {
        const int palisade = cfg.GetUnitIdByName("WALL");
        for (int oy = -kBlock; oy <= kBlock; ++oy) {
            for (int ox = -kBlock; ox <= kBlock; ++ox) {
                if (ECS::Buildings::Place(reg, grid, palisade, kUs, cx + ox, cy + oy, true) == entt::null) {
                    verdict = "no room for the palisade";
                }
            }
        }
        auto& at = reg.get<C::Transform>(villager);
        at.position = at.previous_position = {Fixed32(cx) + Fixed32(1) / Fixed32(2),
                                              Fixed32(cy) + Fixed32(1) / Fixed32(2)};
        reg.remove<C::Path>(villager);
        auto& worker = reg.get<C::Worker>(villager);
        for (int& held : worker.carried) held = 0;
        worker.carry_type = Core::ResourceType::Wood;
        worker.carried[static_cast<int>(Core::ResourceType::Wood)] = 10;
        worker.target_resource = entt::null;
        worker.target_dropoff = entt::null;
        worker.state = Core::UnitState::Returning;
        if (std::strcmp(verdict, "ok") == 0) {
            for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
            if (worker.state != Core::UnitState::Idle) {
                verdict = "a villager with no way to any drop site kept looking for one";
            } else if (worker.carried[static_cast<int>(Core::ResourceType::Wood)] != 10) {
                verdict = "a villager with no way to any drop site lost its load";
            }
        }
    }
    std::printf("dropoffbeyond=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
