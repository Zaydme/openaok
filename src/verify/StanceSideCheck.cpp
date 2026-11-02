#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Cheats.h"
#include "ecs/Diplomacy.h"
#include "ecs/Repairs.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Victory.h"
#include "ecs/components/Health.h"

namespace Verify {

// Three players on no team, teams unlocked, players 1 and 2 made allies in
// play: a villager of player 1's may mend player 2's House and not player
// 3's, and "black death" typed by player 1 puts out player 3 and spares
// player 2.
bool CheckStanceSide() {
    const char* verdict = "ok";
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[3] = {true, 1, 0};
    setup.lock_teams = false;
    setup.cheats = true;
    ECS::Simulation sim;
    sim.Init(setup);
    sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    using ECS::Diplomacy::Stance;
    if (!ECS::Diplomacy::SetStance(reg, 1, 2, Stance::Ally) || !ECS::Diplomacy::SetStance(reg, 2, 1, Stance::Ally)) {
        verdict = "players 1 and 2 could not be made allies";
    }

    // A House of each other player's, damaged, and a villager of player 1's.
    const int house_id = cfg.GetUnitIdByName("HOUS");
    entt::entity houses[4] = {entt::null, entt::null, entt::null, entt::null};
    for (Core::PlayerId p = 2; p <= 3 && std::strcmp(verdict, "ok") == 0; ++p) {
        for (int y = 10; y < grid.GetHeight() - 10 && houses[p] == entt::null; y += 3) {
            for (int x = 10 + 20 * p; x < grid.GetWidth() - 10 && houses[p] == entt::null; x += 3) {
                if (ECS::Buildings::CanPlace(grid, house_id, x, y)) {
                    houses[p] = ECS::Buildings::Place(reg, grid, house_id, p, x, y, true);
                }
            }
        }
        if (houses[p] == entt::null) {
            verdict = "no ground for a House";
        } else {
            auto& health = reg.get<ECS::Components::Health>(houses[p]);
            health.current_hp = health.max_hp / 2;
        }
    }
    const entt::entity villager = ECS::ScenarioWorld::Place(
        reg, grid, cfg.GetUnitIdByName("VMBAS"), 1,
        {Core::Fixed32(grid.GetWidth() / 2), Core::Fixed32(grid.GetHeight() / 2)}, {}, 0);
    if (std::strcmp(verdict, "ok") == 0 && villager == entt::null) verdict = "no villager placed";
    if (std::strcmp(verdict, "ok") == 0) {
        if (!ECS::Repairs::CanRepair(reg, villager, houses[2])) {
            verdict = "a villager may not mend an ally's House";
        } else if (ECS::Repairs::CanRepair(reg, villager, houses[3])) {
            verdict = "a villager may mend an enemy's House";
        }
    }

    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Cheats::Apply(reg, grid, 1, ECS::Cheats::kBlackDeath);
        const auto* outcome = ECS::Victory::Read(reg);
        if (outcome == nullptr || !outcome->Defeated(3)) {
            verdict = "black death spared an enemy";
        } else if (outcome->Defeated(2)) {
            verdict = "black death put out an ally";
        }
    }
    std::printf("stanceside=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
