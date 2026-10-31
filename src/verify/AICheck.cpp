#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ai/Brain.h"
#include "ai/Walls.h"
#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "ecs/Walls.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "verify/AIGame.h"

namespace Verify {

namespace {

// Whether every wall and gate of the computer players stands on the ring the
// player walls (ai/Walls), and how many gates there are in all.
bool WallsOnRings(const entt::registry& reg, int& gates) {
    gates = 0;
    const AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr) return false;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner,
                         const ECS::Components::Transform, const ECS::Components::Building>();
    for (const AI::Brain& b : brains->brains) {
        const int ring = b.wall_perimeter == 1 ? AI::Walls::kNearRing : AI::Walls::kFarRing;
        for (auto e : view) {
            if (view.get<const ECS::Components::Owner>(e).player_id != b.player) continue;
            const int unit = view.get<const ECS::Components::Identity>(e).unit_id;
            if (!ECS::Walls::IsWall(unit) && !ECS::Walls::IsGate(unit)) continue;
            gates += ECS::Walls::IsGate(unit) ? 1 : 0;
            // A gate is four tiles long: its middle is up to two from the line.
            const auto& at = view.get<const ECS::Components::Transform>(e).position;
            const int out = std::max(std::abs(Physics::TileOf(at.x) - b.wall_x), std::abs(Physics::TileOf(at.y) - b.wall_y));
            if (std::abs(out - ring) > (ECS::Walls::IsGate(unit) ? 2 : 0)) return false;
        }
    }
    return true;
}

// The latest minute the hardest computer players may reach the Feudal Age. On
// the default map the Teutons' "castle" plan keeps 25 villagers in the Dark Age
// and trains militia while their gold lasts, and the Franks' "feudal" plan
// stops at 20 and trains militia against the Teutons' raids; they arrive at
// minutes 19 and 17.
constexpr int kFeudalBy = 19;
// Long enough for the later of them to finish the age's 130 seconds.
constexpr int kAIMinutes = 22;

} // namespace

// The computer playing both seats at its hardest: within its first kAIMinutes
// minutes each must grow its town to 20 villagers, reach the Feudal Age by
// kFeudalBy, train soldiers and strike the other's town, unattended; and the
// one whose rules wall its town lays its walls and gates on its perimeter. Soldiers
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
    if (const char* seed = std::getenv("AOE_AI_SEED")) setup.seed = static_cast<uint32_t>(std::atoi(seed));
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
    int gates = 0;
    if (std::strcmp(verdict, "ok") == 0 && !WallsOnRings(sim.GetRegistry(), gates)) {
        verdict = "a computer player laid a wall off its perimeter";
    } else if (std::strcmp(verdict, "ok") == 0 && gates == 0) {
        verdict = "no computer player walled its town with a gate";
    }
    std::printf("ai=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
