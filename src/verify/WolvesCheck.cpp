#include "verify/Harness.h"

#include <cstdio>

#include "ai/Census.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Combat.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kComputer = 2;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kSeconds = 60;
// The two wolves stand this far east of the computer's Town Center, and this
// far north and south of it, in tiles.
constexpr int kWolvesOut = 3;
constexpr int kWolvesApart = 2;
// Ticks from a swing's start by which its blow has landed: the scout strikes
// on its attack's first frame, the tick after.
constexpr int kBlowTicks = 2;

} // namespace

// A computer player's scout and two wolves in its town: every swing the scout
// begins at a wolf lands on it as a blow, even when the wolf has stepped off
// since the swing began.
bool CheckWolves() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.seed = 1;
    setup.difficulty = 2;
    setup.players[kComputer].active = true;
    setup.players[kComputer].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    const AI::Census census = AI::Take(reg, kComputer);
    const entt::entity scout = census.scout;
    entt::entity wolves[2] = {entt::null, entt::null};
    if (census.town_center == entt::null || scout == entt::null) verdict = "no computer town with a scout";
    for (int i = 0; i < 2 && verdict == nullptr; ++i) {
        const Core::Fixed32 north((i * 2 - 1) * kWolvesApart);
        wolves[i] = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("WOLFX"), Core::PLAYER_GAIA,
                                              {census.home.x + Core::Fixed32(kWolvesOut), census.home.y + north},
                                              {}, 0);
        if (wolves[i] == entt::null) verdict = "no wolf placed";
    }
    int swings = 0, missed = 0, last_swing = 0;
    entt::entity aimed = entt::null;
    int due = -1;   // the tick by which the swing's blow is to have landed
    bool landed = false;
    uint64_t seen = 0;
    for (int t = 0; t < kSeconds * kTicksPerSecond && verdict == nullptr; ++t) {
        sim.Update(16);
        if (const ECS::Events::Log* log = ECS::Events::Read(reg)) {
            for (const ECS::Events::Event& e : log->events) {
                if (e.tick <= seen) continue;
                if (e.kind == ECS::Events::Kind::Struck && e.other == scout && e.subject == aimed) landed = true;
            }
            seen = log->tick;
        }
        if (due >= 0 && t >= due) {
            if (!landed && ECS::Combat::Alive(reg, aimed)) ++missed;
            due = -1;
        }
        if (!ECS::Combat::Alive(reg, scout)) break;
        const auto& combat = reg.get<ECS::Components::Combat>(scout);
        if (combat.swing_ms > last_swing && (combat.target == wolves[0] || combat.target == wolves[1])) {
            ++swings;
            aimed = combat.target;
            due = t + kBlowTicks;
            landed = false;
        }
        last_swing = combat.swing_ms;
    }
    if (verdict == nullptr && swings < 3) verdict = "the scout did not take on the wolves";
    else if (verdict == nullptr && missed > 0) verdict = "a scout's swing at a wolf did not wound it";
    std::printf("wolves=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
