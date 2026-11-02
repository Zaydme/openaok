#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "ecs/Victory.h"
#include "net/Wire.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

void Run(ECS::Simulation& sim, int ticks) {
    for (int t = 0; t < ticks; ++t) sim.Update(16);
}

} // namespace

// The lobby's Time and Score: kept through the setup's encoding (the
// network's, a saved or recorded game's); a time limit game of one year ends
// at five seconds and not before; a score game is won by the first player to
// reach its score.
bool CheckLobbyVictory() {
    const char* verdict = "ok";
    Core::GameSetup timed = Core::DefaultSetup();
    timed.victory = Core::VictoryKind::TimeLimit;
    timed.victory_time = 10;   // one year
    timed.victory_score = 7000;
    Net::Wire::Writer out;
    Net::Wire::Write(out, timed);
    Net::Wire::Reader in(out.bytes);
    Core::GameSetup back;
    if (!Net::Wire::Read(in, back) || back.victory_time != 10 || back.victory_score != 7000) {
        verdict = "the lobby's Time and Score are lost in the setup's encoding";
    }

    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Simulation sim;
        sim.Init(timed);
        Run(sim, 4 * kTicksPerSecond);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (outcome == nullptr || outcome->over) {
            verdict = "a lobby's time limit game ended before its time";
        } else {
            Run(sim, 2 * kTicksPerSecond);
            if (!outcome->over || (!outcome->Won(1) && !outcome->Won(2))) {
                verdict = "a lobby's time limit game did not end at its time";
            }
        }
    }

    if (std::strcmp(verdict, "ok") == 0) {
        Core::GameSetup scored = Core::DefaultSetup();
        scored.victory = Core::VictoryKind::Score;
        scored.victory_score = 1;
        ECS::Simulation sim;
        sim.Init(scored);
        Run(sim, 2);
        const auto* outcome = ECS::Victory::Read(sim.GetRegistry());
        if (outcome == nullptr || !outcome->over || !outcome->Won(1)) {
            verdict = "a lobby's score game was not won at its score";
        }
    }
    std::printf("lobbyvictory=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
