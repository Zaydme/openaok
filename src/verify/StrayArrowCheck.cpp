#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Health.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

// An archer's arrow of `owner`'s coming down at `at`, aimed at nobody there:
// a stray.
void Stray(entt::registry& reg, Core::PlayerId owner, const ECS::Components::Vector2D& at) {
    auto& cfg = Core::ConfigManager::Get();
    const auto* archer = Core::Rules::Unit(cfg.GetUnitIdByName("ARCHR"));
    const entt::entity arrow = reg.create();
    reg.emplace<ECS::Components::Transform>(arrow, at, at);
    reg.emplace<ECS::Components::Sprite>(arrow);
    ECS::Components::Projectile flight;
    flight.owner = owner;
    flight.unit_id = archer != nullptr ? archer->projectile : -1;
    flight.from = flight.to = at;
    if (archer != nullptr) {
        for (int i = 0; i < archer->attack_count; ++i) flight.attacks[i] = archer->attacks[i];
        flight.attack_count = archer->attack_count;
    }
    reg.emplace<ECS::Components::Projectile>(arrow, flight);
}

} // namespace

// A stray arrow of player 1's coming down on a spearman of its ally player 2
// leaves it unhurt; one coming down on a spearman of its enemy player 3
// wounds it. Only a blast falls on friends.
bool CheckStrayArrow() {
    const char* verdict = "ok";
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[3] = {true, 1, 0};
    setup.players[1].team = 1;
    setup.players[2].team = 1;
    setup.players[3].team = 2;
    ECS::Simulation sim;
    sim.Init(setup);
    sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const int spear = cfg.GetUnitIdByName("SPRMN");
    const int cx = grid.GetWidth() / 2, cy = grid.GetHeight() / 2;
    const ECS::Components::Vector2D ally_at{Core::Fixed32(cx), Core::Fixed32(cy)};
    const ECS::Components::Vector2D foe_at{Core::Fixed32(cx + 8), Core::Fixed32(cy)};
    const entt::entity ally = ECS::ScenarioWorld::Place(reg, grid, spear, 2, ally_at, {}, 0);
    const entt::entity foe = ECS::ScenarioWorld::Place(reg, grid, spear, 3, foe_at, {}, 0);
    int ally_lost = -1, foe_lost = -1;
    if (ally == entt::null || foe == entt::null) {
        verdict = "the spearmen could not be placed";
    } else {
        const int ally_hp = reg.get<ECS::Components::Health>(ally).current_hp;
        const int foe_hp = reg.get<ECS::Components::Health>(foe).current_hp;
        Stray(reg, 1, reg.get<ECS::Components::Transform>(ally).position);
        Stray(reg, 1, reg.get<ECS::Components::Transform>(foe).position);
        sim.Update(16);
        ally_lost = ally_hp - reg.get<ECS::Components::Health>(ally).current_hp;
        foe_lost = foe_hp - reg.get<ECS::Components::Health>(foe).current_hp;
        if (ally_lost > 0) {
            verdict = "a stray arrow hurt an ally";
        } else if (foe_lost <= 0) {
            verdict = "a stray arrow did not hurt an enemy";
        }
    }
    std::printf("strayarrow=%s ally_lost=%d foe_lost=%d\n", verdict, ally_lost, foe_lost);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
