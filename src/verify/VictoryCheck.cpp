#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Mortality.h"
#include "ecs/Orders.h"
#include "ecs/Simulation.h"
#include "ecs/Victory.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// Destroys everything a player owns.
void Wipe(ECS::Simulation& sim, Core::PlayerId player) {
    auto& reg = sim.GetRegistry();
    std::vector<entt::entity> theirs;
    auto all = reg.view<ECS::Components::Owner, ECS::Components::Health>();
    for (auto e : all) {
        if (all.get<ECS::Components::Owner>(e).player_id == player) theirs.push_back(e);
    }
    for (auto e : theirs) {
        if (reg.valid(e)) ECS::Mortality::Kill(reg, sim.GetGrid(), e);
    }
}

entt::entity TownCenterOf(const entt::registry& reg, Core::PlayerId player) {
    const int tc = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == tc &&
            view.get<const ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

// A finished building of `unit_id` for `player` on open ground near its
// Town Center.
entt::entity Raise(ECS::Simulation& sim, Core::PlayerId player, int unit_id) {
    auto& reg = sim.GetRegistry();
    const entt::entity tc = TownCenterOf(reg, player);
    if (tc == entt::null) return entt::null;
    const auto& at = reg.get<ECS::Components::Transform>(tc).position;
    for (int r = 4; r < 20; ++r) {
        for (int d = -r; d <= r; ++d) {
            const int x = Physics::TileOf(at.x) + d;
            const int y = Physics::TileOf(at.y) + r;
            if (ECS::Buildings::CanPlace(sim.GetGrid(), unit_id, x, y)) {
                return ECS::Buildings::Place(reg, sim.GetGrid(), unit_id, player, x, y, true);
            }
        }
    }
    return entt::null;
}

} // namespace

// The standard victory: conquest, a Wonder standing for its years,
// and every relic held for as long -- each ending the game for the right side,
// and a Wonder lost before its time ending nothing.
bool CheckVictory() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();

    // Conquest: a player with nothing left is defeated and the other wins.
    {
        ECS::Simulation sim;
        sim.Init();
        for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (outcome == nullptr || outcome->over) {
            verdict = "a game was over before anyone had lost anything";
        } else {
            Wipe(sim, 2);
            for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
            if (!outcome->Defeated(2)) verdict = "a player with nothing left was not defeated";
            else if (!outcome->over || !outcome->Won(1)) verdict = "the last player standing did not win";
        }
    }

    // A Wonder standing its years wins; one destroyed first does not.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.wonder_years = 2;   // six seconds
        ECS::Simulation sim;
        sim.Init(setup);
        const entt::entity wonder = Raise(sim, 1, cfg.GetUnitIdByName("WNDR"));
        if (wonder == entt::null) {
            verdict = "no ground to raise a Wonder on";
        } else {
            for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
            const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
            if (outcome->over) verdict = "a Wonder won before its years were up";
            ECS::Buildings::Destroy(sim.GetRegistry(), sim.GetGrid(), wonder);
            for (int t = 0; t < 5 * kTicksPerSecond && ok(); ++t) sim.Update(16);
            if (ok() && outcome->over) verdict = "a Wonder destroyed before its time still won";
            // Raised again, and left standing: its count starts afresh, not
            // from where the lost one's stopped.
            if (ok() && Raise(sim, 1, cfg.GetUnitIdByName("WNDR")) != entt::null) {
                for (int t = 0; t < 4 * kTicksPerSecond; ++t) sim.Update(16);
                if (outcome->over) verdict = "a new Wonder carried on the lost one's count";
                for (int t = 0; t < 4 * kTicksPerSecond && ok(); ++t) sim.Update(16);
                if (ok() && (!outcome->over || !outcome->Won(1) || !outcome->Defeated(2))) {
                    verdict = "a Wonder that stood its years did not win";
                }
            }
        }
    }

    // Every relic on the map in one player's monastery for its years wins.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.relic_years = 2;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        const entt::entity shrine = Raise(sim, 2, cfg.GetUnitIdByName("CRCH"));
        std::vector<entt::entity> relics;
        auto all = reg.view<ECS::Components::Identity, ECS::Components::Transform>();
        for (auto e : all) {
            if (all.get<ECS::Components::Identity>(e).unit_id == cfg.GetUnitIdByName("RELIC")) relics.push_back(e);
        }
        if (shrine == entt::null || relics.empty()) {
            verdict = "no monastery or no relics to try";
        } else {
            // All but one in: nothing happens.
            auto& held = reg.get_or_emplace<ECS::Components::RelicShrine>(shrine);
            for (size_t i = 0; i + 1 < relics.size(); ++i) {
                held.relics.push_back(relics[i]);
                reg.remove<ECS::Components::Transform>(relics[i]);
            }
            for (int t = 0; t < 8 * kTicksPerSecond; ++t) sim.Update(16);
            const auto* outcome = ECS::Victory::Read(reg);
            if (outcome->over) {
                verdict = "holding most of the relics won the game";
            } else {
                reg.get<ECS::Components::RelicShrine>(shrine).relics.push_back(relics.back());
                reg.remove<ECS::Components::Transform>(relics.back());
                for (int t = 0; t < 8 * kTicksPerSecond; ++t) sim.Update(16);
                if (!outcome->over || !outcome->Won(2)) verdict = "holding every relic for its years did not win";
            }
        }
    }

    // Resigning: the player is out at once, through the order the game menu
    // gives, and nothing it had is left standing; a resignation in another's
    // name is refused.
    if (ok()) {
        ECS::Simulation sim;
        sim.Init();
        for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
        auto& reg = sim.GetRegistry();
        const auto* outcome = ECS::Victory::Read(reg);
        auto alive = [&](Core::PlayerId player) {
            int count = 0;
            auto all = reg.view<ECS::Components::Owner, ECS::Components::Health>();
            for (auto e : all) {
                if (all.get<ECS::Components::Owner>(e).player_id == player &&
                    all.get<ECS::Components::Health>(e).current_hp > 0) {
                    ++count;
                }
            }
            return count;
        };
        const int before = alive(2);
        ECS::Orders::Execute(reg, sim.GetGrid(), Core::ResignCommand{2});
        if (before == 0) {
            verdict = "player 2 had nothing to lose before resigning";
        } else if (!outcome->Defeated(2)) {
            verdict = "a player that resigned was not out of the game";
        } else if (alive(2) != 0) {
            verdict = "a player that resigned kept units or buildings";
        } else {
            for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
            if (!outcome->over || !outcome->Won(1)) verdict = "the player left after a resignation did not win";
        }
    }

    // Deleting a unit: one of the player's own dies, one of another's does not.
    if (ok()) {
        ECS::Simulation sim;
        sim.Init();
        auto& reg = sim.GetRegistry();
        auto first_unit_of = [&](Core::PlayerId player) {
            auto all = reg.view<ECS::Components::Owner, ECS::Components::Health>();
            for (auto e : all) {
                if (all.get<ECS::Components::Owner>(e).player_id == player &&
                    !reg.all_of<ECS::Components::Building>(e)) {
                    return e;
                }
            }
            return entt::entity{entt::null};
        };
        const entt::entity mine = first_unit_of(1);
        const entt::entity theirs = first_unit_of(2);
        if (mine == entt::null || theirs == entt::null) {
            verdict = "no units to delete";
        } else {
            ECS::Orders::Execute(reg, sim.GetGrid(), Core::DeleteCommand{theirs, 1});
            ECS::Orders::Execute(reg, sim.GetGrid(), Core::DeleteCommand{mine, 1});
            if (reg.get<ECS::Components::Health>(theirs).current_hp <= 0) {
                verdict = "a player deleted another player's unit";
            } else if (reg.valid(mine) && reg.all_of<ECS::Components::Health>(mine) &&
                       reg.get<ECS::Components::Health>(mine).current_hp > 0) {
                verdict = "a unit its owner deleted did not die";
            }
        }
    }

    std::printf("victory=%s\n", verdict);
    return ok();
}

} // namespace Verify
