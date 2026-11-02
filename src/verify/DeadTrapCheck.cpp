#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kPlayer = 1;

// The player's entities of `unit_id`, by their tile.
std::vector<entt::entity> OfAt(const entt::registry& reg, int unit_id, int x, int y) {
    std::vector<entt::entity> out;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner,
                         const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id != unit_id) continue;
        if (view.get<const ECS::Components::Owner>(e).player_id != kPlayer) continue;
        const auto& p = view.get<const ECS::Components::Transform>(e).position;
        if (Physics::TileOf(p.x) == x && Physics::TileOf(p.y) == y) out.push_back(e);
    }
    return out;
}

} // namespace

// A fish trap run out by a Fishing Ship leaves its dead unit, the dat's
// FTRAP_D, its owner's; the ship ordered to build a trap there puts a new one
// up in its place, the remains gone.
bool CheckDeadTrap() {
    const char* verdict = "ok";
    auto& cfg = Core::ConfigManager::Get();
    const int trap_id = cfg.GetUnitIdByName("FTRAP");
    const int dead_id = cfg.GetUnitIdByName("FTRAP_D");
    const int ship_id = cfg.GetUnitIdByName("FSHSP");
    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Islands";
    setup.starting_age = 1;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const Physics::Layer sea = Shore::Sea(sim);
    const auto [sx, sy] = sim.player_starts[0];
    int tx = 0, ty = 0;
    entt::entity trap = entt::null, ship = entt::null;
    if (trap_id < 0 || dead_id < 0 || ship_id < 0) {
        verdict = "the dat has no fish trap, dead fish trap or fishing ship";
    } else if (!Shore::NearestSpot(grid, trap_id, sx, sy, 40, tx, ty) || !grid.Passable(sea, tx, ty)) {
        verdict = "no water near the start takes a fish trap";
    } else {
        trap = ECS::Buildings::Place(reg, grid, trap_id, kPlayer, tx, ty, true);
        const auto& at = reg.get<ECS::Components::Transform>(trap).position;
        ship = ECS::Units::Spawn(reg, ship_id, kPlayer, {at.x + Core::Fixed32(2), at.y});
        if (auto* me = ECS::Players::Find(reg, kPlayer)) ECS::Players::AddStock(*me, Core::ResourceType::Wood, 1000);
        // A trap nearly run out.
        reg.get<ECS::Components::Resource>(trap).current_amount = 2;
    }
    bool left = false, rebuilt = false;
    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Orders::Execute(reg, grid, Core::GatherCommand{ship, trap, kPlayer});
        for (int t = 0; t < 60 * Shore::kTicksPerSecond && !left; ++t) {
            sim.Update(16);
            left = !reg.valid(trap) && !OfAt(reg, dead_id, tx, ty).empty();
        }
        if (!left) {
            verdict = "a fish trap run out left nothing to rebuild";
        } else {
            Core::CommandQueue::Get().PushCommand(Core::BuildCommand{ship, trap_id, tx, ty, kPlayer});
            sim.Update(16);
            rebuilt = !OfAt(reg, trap_id, tx, ty).empty() && OfAt(reg, dead_id, tx, ty).empty();
            if (!rebuilt) verdict = "a fish trap was not put up again over its remains";
        }
    }
    std::printf("deadtrap=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
