#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Allegiance.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// A spearman of player 2's converted to player 1 and killed: its body lies
// in player 1's colour, while it still plays by player 2's rules until then.
bool CheckCorpseColour() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const int x = grid.GetWidth() / 2, y = grid.GetHeight() / 2;
    const ECS::Components::Vector2D at{Core::Fixed32(x), Core::Fixed32(y)};
    const entt::entity spearman = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("SPRMN"), 2, at, {}, 0);
    Core::PlayerId body_owner = 0;
    if (spearman == entt::null) {
        verdict = "no spearman placed";
    } else {
        ECS::Allegiance::Change(reg, grid, spearman, 1);
        if (ECS::Players::RulesPlayer(reg, spearman) != 2) verdict = "a converted unit does not keep its maker's rules";
        ECS::Mortality::Kill(reg, grid, spearman);
        for (int t = 0; t < 6 * kTicksPerSecond; ++t) sim.Update(16);
        auto bodies = reg.view<const ECS::Components::Corpse, const ECS::Components::Owner,
                               const ECS::Components::Transform>();
        for (auto b : bodies) {
            const auto& p = bodies.get<const ECS::Components::Transform>(b).position;
            const Core::Fixed32 dx = p.x - at.x, dy = p.y - at.y;
            if (dx * dx + dy * dy <= Core::Fixed32(1)) body_owner = bodies.get<const ECS::Components::Owner>(b).player_id;
        }
        if (std::strcmp(verdict, "ok") == 0 && body_owner == 0) verdict = "the converted unit left no body";
        if (std::strcmp(verdict, "ok") == 0 && body_owner != 1) {
            verdict = "a converted unit's body lies in its maker's colour";
        }
    }
    std::printf("corpsecolour=%s owner=%d\n", verdict, static_cast<int>(body_owner));
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
