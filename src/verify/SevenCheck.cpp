#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ai/Brain.h"
#include "ai/Census.h"
#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "verify/AIGame.h"

namespace Verify {

// The lobby seats up to seven computer players beside the human.
// A full table on a medium map: every computer player has its brain, its
// scripts loaded whole under its own civilisation, and in three minutes has
// grown its town -- villagers trained, a house or a mill put up.
bool CheckSeven() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_size = 168;
    setup.difficulty = 2;
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
        setup.players[seat].active = true;
        setup.players[seat].civ = 1 + (seat * 5) % 13;
        setup.players[seat].computer = seat != 1;
    }
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    const char* verdict = nullptr;
    const AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.size() != 7) verdict = "not every computer seat has a brain";
    if (verdict == nullptr) {
        for (const AI::Brain& b : brains->brains) {
            if (!b.program.errors.empty() || b.program.rules.size() < 400) verdict = "a computer seat's scripts did not load";
        }
    }
    int buildings[Core::kMaxPlayers] = {};
    for (Core::PlayerId p = 2; p < Core::kMaxPlayers; ++p) {
        buildings[p] = static_cast<int>(AI::Take(reg, p).buildings.size());
    }
    AIGame::Progress progress;
    constexpr int kMinutes = 3;
    for (int minute = 1; minute <= kMinutes && verdict == nullptr; ++minute) {
        AIGame::Minute(sim, nullptr, minute, progress);
        if (std::getenv("AOE_AI_TRACE") != nullptr) {
            for (Core::PlayerId p = 2; p < Core::kMaxPlayers; ++p) AIGame::Trace(reg, minute, p);
        }
    }
    for (Core::PlayerId p = 2; p < Core::kMaxPlayers && verdict == nullptr; ++p) {
        const AI::Census c = AI::Take(reg, p);
        if (progress.peak_villagers[p] < 7) verdict = "a computer player of seven did not train its villagers";
        else if (static_cast<int>(c.buildings.size()) <= buildings[p]) verdict = "a computer player of seven built nothing";
    }
    std::printf("seven=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
