#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kArchers = 10;
// Open ground west of the town on the default map.
constexpr int kX = 70;
constexpr int kY = 64;

entt::entity Tough(entt::registry& reg, int unit_id, Core::PlayerId player, int x, int y) {
    const entt::entity e = ECS::Units::Spawn(reg, unit_id, player, {Core::Fixed32(x), Core::Fixed32(y)});
    reg.get<ECS::Components::Health>(e).max_hp = reg.get<ECS::Components::Health>(e).current_hp = 100000;
    return e;
}

// Whether a helper of `stance` five tiles from a struck spearman comes to
// help, its attacker an archer shooting from four tiles past it, beyond the
// helper's own sight.
bool Helps(ECS::Components::Stance stance, int archer, int spear) {
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    const entt::entity helper = ECS::Units::Spawn(reg, archer, 1, {Core::Fixed32(kX), Core::Fixed32(kY)});
    reg.get<ECS::Components::Combat>(helper).stance = stance;
    const entt::entity victim = Tough(reg, spear, 1, kX + 5, kY);
    reg.get<ECS::Components::Combat>(victim).stance = ECS::Components::Stance::NoAttack;
    const entt::entity attacker = Tough(reg, archer, 2, kX + 9, kY);
    Core::CommandQueue::Get().PushCommand(Core::AttackCommand{attacker, victim, 2});
    for (int t = 0; t < 10 * kTicksPerSecond; ++t) {
        sim.Update(16);
        const auto& combat = reg.get<ECS::Components::Combat>(helper);
        if (combat.target == attacker && combat.state != ECS::Components::CombatState::Idle) return true;
    }
    return false;
}

} // namespace

// The fight as it feels: ten archers set at one enemy in the same tick loose
// their first arrows over a spread of ticks, not all at once; and an
// aggressive archer comes to help one of its side struck five tiles off, by
// an archer beyond its own sight, where a defensive one stays.
bool CheckAggression() {
    const char* verdict = "ok";
    auto& cfg = Core::ConfigManager::Get();
    const int archer = cfg.GetUnitIdByName("ARCHR");
    const int spear = cfg.GetUnitIdByName("SPRMN");

    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    std::vector<entt::entity> archers;
    // A column half a tile apart, every one already within range of it.
    for (int i = 0; i < kArchers; ++i) {
        const Core::Fixed32 y = Core::Fixed32(kY) + Core::Fixed32(i * 2 - (kArchers - 1)) / Core::Fixed32(4);
        archers.push_back(ECS::Units::Spawn(reg, archer, 1, {Core::Fixed32(kX + 1), y}));
    }
    const entt::entity target = Tough(reg, spear, 2, kX + 3, kY);
    reg.get<ECS::Components::Combat>(target).stance = ECS::Components::Stance::NoAttack;
    for (auto e : archers) Core::CommandQueue::Get().PushCommand(Core::AttackCommand{e, target, 1});
    // The tick each archer begins its first shot: its swing begun.
    std::vector<int> first(archers.size(), -1);
    for (int t = 0; t < 5 * kTicksPerSecond; ++t) {
        sim.Update(16);
        for (size_t i = 0; i < archers.size(); ++i) {
            if (first[i] < 0 && reg.get<ECS::Components::Combat>(archers[i]).strike_ms >= 0) first[i] = t;
        }
    }
    std::set<int> ticks(first.begin(), first.end());
    const int spread = *std::max_element(first.begin(), first.end()) - *std::min_element(first.begin(), first.end());
    const bool aggressive = Helps(ECS::Components::Stance::Aggressive, archer, spear);
    const bool defensive = Helps(ECS::Components::Stance::Defensive, archer, spear);

    if (ticks.count(-1) != 0) verdict = "an archer set at the enemy never loosed";
    else if (ticks.size() < kArchers / 2 || spread < 10) verdict = "archers set at one enemy together all loosed at once";
    else if (!aggressive) verdict = "an aggressive archer did not come to help one of its side in its sight";
    else if (defensive) verdict = "a defensive archer came to help from beyond three tiles";
    std::printf("aggression=%s first_ticks=%zu spread_ticks=%d aggressive_helps=%d defensive_helps=%d\n", verdict,
                ticks.size(), spread, aggressive ? 1 : 0, defensive ? 1 : 0);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
