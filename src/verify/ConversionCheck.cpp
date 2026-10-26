#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Allegiance.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Monks.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Units.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr int kMeleeClass = 4;
constexpr int kPierceClass = 3;

int Attack(const Core::Rules::UnitRules* rules) {
    return rules != nullptr
        ? Core::Rules::ClassValue(rules->attacks, rules->attack_count, kMeleeClass, 0) : -1;
}

int Armour(const Core::Rules::UnitRules* rules) {
    return rules != nullptr
        ? Core::Rules::ClassValue(rules->armours, rules->armour_count, kPierceClass, 0) : -1;
}

int TechNamed(const char* label) {
    const auto& techs = Core::Rules::Get().techs;
    for (int id = 0; id < static_cast<int>(techs.size()); ++id) {
        if (techs[id].valid && Core::Rules::TechLabel(id) == label) return id;
    }
    return -1;
}

} // namespace

// A converted unit keeps the technology of the side that made it. Player 1
// researches Forging and trains a militia; converted to player 2, who has no
// Forging, it still strikes with Forging's extra point. Player 2's Scale Mail
// does not reach it; player 1's later Scale Mail does, since the unit plays by
// its maker's table; and player 1's Man-at-Arms upgrade turns it into a
// man-at-arms where it stands under its new owner, hit points and all.
// Converted back to its maker, it is simply its own again.
bool CheckConversion() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int militia = cfg.GetUnitIdByName("SPRMN");
    const int forging = TechNamed("Forging");
    const int scale_mail = TechNamed("Scale Mail Armor");
    const int man_at_arms = TechNamed("Man-at-Arms");
    if (forging < 0 || scale_mail < 0 || man_at_arms < 0) verdict = "the dat has no Forging, Scale Mail or Man-at-Arms";

    Core::GameSetup setup = Core::DefaultSetup();
    setup.starting_age = 1;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();

    if (ok()) {
        ECS::Techs::Complete(reg, 1, forging);
        const auto& start = sim.player_starts[0];
        const entt::entity unit = ECS::Units::Spawn(
            reg, militia, 1, {Core::Fixed32(start.first + 6), Core::Fixed32(start.second + 6)});
        const int ours = Attack(ECS::Players::UnitOf(reg, 1, militia));
        const int theirs = Attack(ECS::Players::UnitOf(reg, 2, militia));
        if (ours <= theirs) verdict = "Forging did not raise player 1's militia";
        ECS::Allegiance::Change(reg, grid, unit, 2);
        if (ok() && reg.get<ECS::Components::Owner>(unit).player_id != 2) verdict = "the militia was not converted";
        else if (ok() && Attack(ECS::Combat::FightingRules(reg, unit)) != ours) {
            verdict = "a converted unit lost its maker's technology";
        }
        // The new owner's technology does not reach it; its maker's does.
        if (ok()) {
            const int armour = Armour(ECS::Combat::DefendingRules(reg, unit));
            ECS::Techs::Complete(reg, 2, scale_mail);
            if (Armour(ECS::Combat::DefendingRules(reg, unit)) != armour) {
                verdict = "a converted unit took its new owner's technology";
            }
            ECS::Techs::Complete(reg, 1, scale_mail);
            if (ok() && Armour(ECS::Combat::DefendingRules(reg, unit)) <= armour) {
                verdict = "a converted unit missed its maker's later technology";
            }
        }
        // Its maker's upgrade of the line upgrades it where it stands.
        if (ok()) {
            ECS::Techs::Complete(reg, 1, man_at_arms);
            const auto* upgraded = ECS::Players::UnitOf(reg, 1, militia);
            const auto& hp = reg.get<ECS::Components::Health>(unit);
            if (upgraded == nullptr || hp.max_hp != upgraded->hp) {
                verdict = "a converted unit was not upgraded with its maker's line";
            } else if (Attack(ECS::Combat::FightingRules(reg, unit)) != Attack(upgraded)) {
                verdict = "a converted unit does not strike as its maker's upgrade";
            }
        }
        // Home again, it is its own.
        if (ok()) {
            ECS::Allegiance::Change(reg, grid, unit, 1);
            if (reg.all_of<ECS::Components::RulesOf>(unit)) verdict = "a unit converted back still plays by another's table";
        }
    }

    // What a monk may convert: buildings and siege weapons only with
    // Redemption, and never a Town Center, Castle, Monastery, Wonder, farm,
    // wall, tower or gate.
    if (ok()) {
        auto& cfg2 = Core::ConfigManager::Get();
        const auto& start = sim.player_starts[1];
        auto at = [&](int dx, int dy) {
            return ECS::Components::Vector2D{Core::Fixed32(start.first + dx),
                                             Core::Fixed32(start.second + dy)};
        };
        const entt::entity monk = ECS::Units::Spawn(reg, cfg2.GetUnitIdByName("MONKX"), 2, at(0, 6));
        const entt::entity ram = ECS::Units::Spawn(reg, cfg2.GetUnitIdByName("BTRAM"), 1, at(2, 6));
        entt::entity house = entt::null, tower = entt::null, town_center = entt::null;
        for (const auto& [name, out] : {std::pair<const char*, entt::entity*>{"HOUS", &house},
                                        std::pair<const char*, entt::entity*>{"WCTW", &tower}}) {
            const int id = cfg2.GetUnitIdByName(name);
            for (int r = 4; r < 20 && *out == entt::null; ++r) {
                for (int d = -r; d <= r && *out == entt::null; ++d) {
                    const int x = start.first + d, y = start.second - r;
                    if (ECS::Buildings::CanPlace(grid, id, x, y)) {
                        *out = ECS::Buildings::Place(reg, grid, id, 1, x, y, true);
                    }
                }
            }
        }
        auto tcs = reg.view<ECS::Components::Identity, ECS::Components::Owner>();
        for (auto e : tcs) {
            if (tcs.get<ECS::Components::Owner>(e).player_id == 1 &&
                tcs.get<ECS::Components::Identity>(e).unit_id == cfg2.GetUnitIdByName("RTWC")) {
                town_center = e;
            }
        }
        if (ECS::Monks::CanConvert(reg, monk, house) || ECS::Monks::CanConvert(reg, monk, ram)) {
            verdict = "a monk without Redemption could convert a building or a ram";
        }
        auto* two = ECS::Players::Find(reg, 2);
        ECS::Players::SetResource(*two, 28, Core::Fixed32(1));   // Redemption's grant
        if (ok() && (!ECS::Monks::CanConvert(reg, monk, house) || !ECS::Monks::CanConvert(reg, monk, ram))) {
            verdict = "a monk with Redemption could not convert a House or a ram";
        } else if (ok() && (ECS::Monks::CanConvert(reg, monk, tower) ||
                            ECS::Monks::CanConvert(reg, monk, town_center))) {
            verdict = "a monk with Redemption could convert a tower or a Town Center";
        }
    }

    std::printf("converted=%s\n", verdict);
    return ok();
}

} // namespace Verify
