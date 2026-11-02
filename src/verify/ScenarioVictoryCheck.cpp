#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Score.h"
#include "ecs/Simulation.h"
#include "ecs/Victory.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Transform.h"
#include "scenario/Scenario.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// A finished building of `unit_id` for `player` on open ground near its
// Town Center.
entt::entity Raise(ECS::Simulation& sim, Core::PlayerId player, int unit_id) {
    auto& reg = sim.GetRegistry();
    const int tc_id = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner,
                         const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id != tc_id) continue;
        if (view.get<const ECS::Components::Owner>(e).player_id != player) continue;
        const auto& at = view.get<const ECS::Components::Transform>(e).position;
        for (int r = 5; r < 25; ++r) {
            for (int d = -r; d <= r; ++d) {
                const int x = Physics::TileOf(at.x) + d, y = Physics::TileOf(at.y) + r;
                if (ECS::Buildings::CanPlace(sim.GetGrid(), unit_id, x, y)) {
                    return ECS::Buildings::Place(reg, sim.GetGrid(), unit_id, player, x, y, true);
                }
            }
        }
    }
    return entt::null;
}

void Run(ECS::Simulation& sim, int ticks) {
    for (int t = 0; t < ticks; ++t) sim.Update(16);
}

int32_t TotalOf(const ECS::Simulation& sim, Core::PlayerId player) {
    const ECS::Score::Sheet* sheet = ECS::Score::Of(sim.GetRegistry(), player);
    return sheet != nullptr ? sheet->total : 0;
}

// A game of `kind` with `goal`, player 2 given a Castle, so the higher score.
void Start(ECS::Simulation& sim, Core::VictoryKind kind, const ECS::Victory::Goal& goal) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.victory = kind;
    sim.Init(setup);
    ECS::Victory::SetGoal(sim.GetRegistry(), goal);
    Raise(sim, 2, Core::ConfigManager::Get().GetUnitIdByName("CSTL"));
    Run(sim, 2);
}

} // namespace

// A scenario's own victory settings, played: its Score, Time Limit and Custom
// modes reach the game's setup; a score game is won by the first to reach
// its score; a time limit game, at its time, by the highest score; a custom
// game by its relics held, and, when it asks for all its conditions, not by
// the relics alone.
bool CheckScenarioVictory() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };

    Scenario::File file;
    Core::GameSetup from_file = Core::DefaultSetup();
    const int modes[3] = {Scenario::VictoryMode::Score, Scenario::VictoryMode::Timed, Scenario::VictoryMode::Custom};
    const Core::VictoryKind kinds[3] = {Core::VictoryKind::Score, Core::VictoryKind::TimeLimit,
                                        Core::VictoryKind::Custom};
    for (int i = 0; i < 3 && ok(); ++i) {
        file.victory.mode = modes[i];
        Scenario::SetupFor(file, from_file);
        if (from_file.victory != kinds[i]) verdict = "a scenario's victory mode did not reach its game";
    }

    // Score: nothing at a score nobody has; player 2, the higher, at its own.
    if (ok()) {
        ECS::Simulation sim;
        ECS::Victory::Goal goal;
        goal.score = 1000000;
        Start(sim, Core::VictoryKind::Score, goal);
        Run(sim, kTicksPerSecond);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        const int32_t one = TotalOf(sim, 1), two = TotalOf(sim, 2);
        if (outcome->over) {
            verdict = "a score game was won below its score";
        } else if (two <= one) {
            verdict = "a Castle did not raise its player's score";
        } else {
            goal.score = two;
            ECS::Victory::SetGoal(sim.GetRegistry(), goal);
            Run(sim, 2);
            if (!outcome->over || !outcome->Won(2) || outcome->Won(1)) verdict = "a score game was not won at its score";
        }
    }

    // Time Limit: two years, six seconds; the higher score wins at the time.
    if (ok()) {
        ECS::Simulation sim;
        ECS::Victory::Goal goal;
        goal.time = 20;
        Start(sim, Core::VictoryKind::TimeLimit, goal);
        Run(sim, 5 * kTicksPerSecond);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (outcome->over) verdict = "a time limit game ended before its time";
        Run(sim, 2 * kTicksPerSecond);
        if (ok() && (!outcome->over || !outcome->Won(2) || outcome->Won(1))) {
            verdict = "a time limit game was not won by the highest score at its time";
        }
    }

    // Custom: one relic held wins at once; with "all" and an exploration too,
    // the relic alone does not.
    for (int all = 0; all < 2 && ok(); ++all) {
        ECS::Simulation sim;
        ECS::Victory::Goal goal;
        goal.conquest = false;
        goal.relics = 1;
        goal.explored = all ? 100 : 0;
        goal.all = all != 0;
        Start(sim, Core::VictoryKind::Custom, goal);
        auto& reg = sim.GetRegistry();
        const entt::entity monastery = Raise(sim, 1, Core::ConfigManager::Get().GetUnitIdByName("CRCH"));
        entt::entity relic = entt::null;
        auto relics = reg.view<ECS::Components::Identity, ECS::Components::Transform>();
        for (auto e : relics) {
            if (relics.get<ECS::Components::Identity>(e).unit_id == Core::ConfigManager::Get().GetUnitIdByName("RELIC")) {
                relic = e;
            }
        }
        if (monastery == entt::null || relic == entt::null) {
            verdict = "no monastery or relic to try";
            break;
        }
        const auto* outcome = ECS::Victory::Read(reg);
        Run(sim, kTicksPerSecond);
        if (outcome->over) {
            verdict = "a custom game was won with no relic held";
            break;
        }
        reg.get_or_emplace<ECS::Components::RelicShrine>(monastery).relics.push_back(relic);
        reg.remove<ECS::Components::Transform>(relic);
        Run(sim, 2);
        if (!all && (!outcome->over || !outcome->Won(1))) verdict = "a custom game was not won by its relics";
        if (all && outcome->over) verdict = "a custom game asking for all its conditions was won by one";
    }
    std::printf("scenariovictory=%s\n", verdict);
    return ok();
}

} // namespace Verify
