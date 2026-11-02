#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/InputManager.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Worker.h"
#include "rendering/Renderer.h"

namespace Verify {

// Two spearmen of player 1's by a villager, one set to guard the villager,
// the player's other soldiers gone: the idle military button, pressed again
// and again, finds the other spearman and never the guard.
bool CheckIdleMilitary() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    std::vector<entt::entity> soldiers;
    auto fighters = reg.view<const ECS::Components::Combat, const ECS::Components::Owner>();
    for (auto e : fighters) {
        if (fighters.get<const ECS::Components::Owner>(e).player_id != 1) continue;
        if (reg.any_of<ECS::Components::Worker, ECS::Components::Building>(e)) continue;
        soldiers.push_back(e);
    }
    for (entt::entity e : soldiers) reg.destroy(e);
    const int x = grid.GetWidth() / 2, y = grid.GetHeight() / 2;
    auto place = [&](const char* name, int dx) {
        return ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(name), 1,
                                         {Core::Fixed32(x + dx), Core::Fixed32(y)}, {}, 0);
    };
    const entt::entity villager = place("VMBAS", 0);
    const entt::entity guard = place("SPRMN", 2);
    const entt::entity idle = place("SPRMN", 4);
    if (villager == entt::null || guard == entt::null || idle == entt::null) {
        verdict = "the units could not be placed";
    } else {
        ECS::Orders::Execute(reg, grid, Core::GuardCommand{{guard}, villager, 1});
        for (int t = 0; t < 10; ++t) sim.Update(16);
        Rendering::Renderer view;
        auto& input = Core::InputManager::Get();
        int found_idle = 0, found_guard = 0;
        for (int press = 0; press < 4; ++press) {
            input.NextIdleMilitary(sim, view);
            found_idle += reg.get<ECS::Components::Selectable>(idle).is_selected ? 1 : 0;
            found_guard += reg.get<ECS::Components::Selectable>(guard).is_selected ? 1 : 0;
        }
        if (found_guard > 0) {
            verdict = "the idle military button found a unit on guard";
        } else if (found_idle == 0) {
            verdict = "the idle military button did not find the idle spearman";
        }
    }
    std::printf("idlemilitary=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
