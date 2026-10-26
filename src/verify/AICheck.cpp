#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "verify/AIGame.h"

namespace Verify {

namespace {

// The latest minute the hardest computer players may reach the Feudal Age. On
// the default map the Teutons' "castle" plan keeps 25 villagers in the Dark Age
// and trains militia while their gold lasts, and the Franks' "feudal" plan
// stops at 20; they arrive at minutes 19 and 21.
constexpr int kFeudalBy = 21;
// Long enough for the later of them to finish the age's 130 seconds.
constexpr int kAIMinutes = 22;

} // namespace

// The computer playing both seats at its hardest: within its first kAIMinutes
// minutes each must grow its town to 20 villagers, reach the Feudal Age by
// kFeudalBy, train soldiers and strike the other's town, unattended. Soldiers
// are counted as trained, not as standing. A twin game alongside must come out
// identical, since in a lockstep game every machine runs its own copy of the
// computer players.
bool CheckAI() {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = true;
    setup.players[2].computer = true;
    setup.difficulty = 0;
    // For tuning: another difficulty or map (the gate never sets them).
    if (const char* d = std::getenv("AOE_AI_DIFFICULTY")) setup.difficulty = std::atoi(d);
    if (const char* m = std::getenv("AOE_AI_MAP")) setup.map_script = m;
    ECS::Simulation sim;
    sim.Init(setup);
    ECS::Simulation twin;
    twin.Init(setup);
    const bool trace = std::getenv("AOE_AI_TRACE") != nullptr;

    int kMinutes = kAIMinutes;
    if (const char* m = std::getenv("AOE_AI_MINUTES")) kMinutes = std::atoi(m);
    AIGame::Progress progress;
    bool diverged = false;
    for (int minute = 1; minute <= kMinutes; ++minute) {
        if (!AIGame::Minute(sim, &twin, minute, progress)) diverged = true;
        if (trace) {
            for (Core::PlayerId p = 1; p <= 2; ++p) AIGame::Trace(sim.GetRegistry(), minute, p);
        }
    }

    if (trace) {
        std::printf("AI trained p1 %d p2 %d, struck %d, feudal %d/%d\n", progress.trained[1],
                    progress.trained[2], progress.struck_enemy ? 1 : 0, progress.feudal_minute[1],
                    progress.feudal_minute[2]);
    }
    const char* verdict = "ok";
    for (Core::PlayerId p = 1; p <= 2 && std::strcmp(verdict, "ok") == 0; ++p) {
        if (progress.peak_villagers[p] < 20) verdict = "a computer player never grew its town";
        else if (progress.feudal_minute[p] < 0) verdict = "a computer player never reached the Feudal Age";
        else if (progress.feudal_minute[p] > kFeudalBy) verdict = "a hardest computer player reached the Feudal Age late";
        else if (progress.trained[p] < 5) verdict = "a computer player never raised an army";
    }
    if (std::strcmp(verdict, "ok") == 0 && diverged) {
        verdict = "two identical games played by the computer came out differently";
    }
    if (std::strcmp(verdict, "ok") == 0 && !progress.struck_enemy) {
        verdict = "neither computer player ever attacked the other's town";
    }
    std::printf("ai=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
