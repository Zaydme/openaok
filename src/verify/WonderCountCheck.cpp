#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Simulation.h"
#include "ecs/Victory.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// A finished building of `unit_id` for `player` on open ground near its
// Town Center.
entt::entity Raise(ECS::Simulation& sim, Core::PlayerId player, int unit_id) {
    auto& reg = sim.GetRegistry();
    const int tc_id = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    entt::entity tc = entt::null;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == tc_id &&
            view.get<const ECS::Components::Owner>(e).player_id == player) {
            tc = e;
        }
    }
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

void Run(ECS::Simulation& sim, int ticks) {
    for (int t = 0; t < ticks; ++t) sim.Update(16);
}

} // namespace

// A Wonder and the relics count down the map size's years, 200 on the
// smallest maps to 400 on a Giant one, of five seconds of game time each.
// Each player's Wonder counts its own years: player 1's, finished three
// seconds before player 2's, wins at its time, both counting meanwhile. The
// relics' count goes on when they pass from one ally's Monastery to the
// other's, and the side wins at its time.
bool CheckWonderCount() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();

    {
        static constexpr int kYears[] = {200, 200, 200, 300, 350, 400};
        Core::GameSetup sized = Core::DefaultSetup();
        for (int i = 0; i < 6 && ok(); ++i) {
            sized.map_size = Core::kMapSizes[i].tiles;
            if (Core::VictoryYears(sized, true) != kYears[i] || Core::VictoryYears(sized, false) != kYears[i]) {
                verdict = "a map size's Wonder and relic years are not the original's";
            }
        }
        // A Normal map's Wonder: 300 years of five seconds from when it stands.
        sized.map_size = Core::kMapSizes[3].tiles;
        ECS::Simulation sim;
        sim.Init(sized);
        if (ok() && Raise(sim, 1, cfg.GetUnitIdByName("WNDR")) == entt::null) verdict = "no ground for a Wonder";
        Run(sim, kTicksPerSecond);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        const int32_t left = outcome != nullptr ? outcome->wonder_ms[1] : 0;
        if (ok() && (left > 300 * 5000 - 900 || left < 300 * 5000 - 1100)) {
            verdict = "a Wonder's countdown is not 300 years of five seconds on a Normal map";
        }
    }

    {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.wonder_years = 2;   // ten seconds
        ECS::Simulation sim;
        sim.Init(setup);
        const int wonder = cfg.GetUnitIdByName("WNDR");
        if (Raise(sim, 1, wonder) == entt::null) verdict = "no ground for player 1's Wonder";
        Run(sim, 3 * kTicksPerSecond);
        if (ok() && Raise(sim, 2, wonder) == entt::null) verdict = "no ground for player 2's Wonder";
        Run(sim, 5 * kTicksPerSecond);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (ok() && (outcome->over || !outcome->CountingWonder(1) || !outcome->CountingWonder(2))) {
            verdict = "two players' Wonders are not both counting";
        }
        Run(sim, 4 * kTicksPerSecond);
        if (ok() && (!outcome->over || !outcome->Won(1) || outcome->Won(2))) {
            verdict = "the first Wonder finished did not win at its time";
        }
    }

    {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.players[3] = {true, 1, 0};
        setup.players[1].team = 1;
        setup.players[2].team = 1;
        setup.players[3].team = 2;
        setup.relic_years = 2;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        const int monastery = cfg.GetUnitIdByName("CRCH");
        const entt::entity first = Raise(sim, 1, monastery);
        const entt::entity second = Raise(sim, 2, monastery);
        std::vector<entt::entity> relics;
        auto all = reg.view<ECS::Components::Identity, ECS::Components::Transform>();
        for (auto e : all) {
            if (all.get<ECS::Components::Identity>(e).unit_id == cfg.GetUnitIdByName("RELIC")) relics.push_back(e);
        }
        if (ok() && (first == entt::null || second == entt::null || relics.empty())) {
            verdict = "no monasteries or no relics to try";
        }
        if (ok()) {
            auto& held = reg.get_or_emplace<ECS::Components::RelicShrine>(first);
            for (entt::entity relic : relics) {
                held.relics.push_back(relic);
                reg.remove<ECS::Components::Transform>(relic);
            }
            Run(sim, 3 * kTicksPerSecond);
            // Over to the ally.
            auto& to = reg.get_or_emplace<ECS::Components::RelicShrine>(second);
            auto& from = reg.get<ECS::Components::RelicShrine>(first);
            to.relics = from.relics;
            from.relics.clear();
            Run(sim, 5 * kTicksPerSecond);
            const auto* outcome = ECS::Victory::Read(reg);
            if (outcome->over) verdict = "holding the relics won before their time";
            Run(sim, 4 * kTicksPerSecond);
            if (ok() && (!outcome->over || !outcome->Won(1) || !outcome->Won(2))) {
                verdict = "the relics' count started again when they passed to an ally";
            }
        }
    }
    std::printf("wondercount=%s\n", verdict);
    return ok();
}

} // namespace Verify
