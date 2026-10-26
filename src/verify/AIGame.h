#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/Simulation.h"

// What the checks of the computer players share: a game played minute by
// minute with what each player did noted, and the trace that shows the
// rules' state for tuning (AOE_AI_TRACE=1).
namespace Verify::AIGame {

constexpr int kTicksPerMinute = 60 * 1000 / 16;

struct Progress {
    int feudal_minute[Core::kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    int peak_villagers[Core::kMaxPlayers] = {};
    int trained[Core::kMaxPlayers] = {};        // units born that are not villagers
    bool struck_enemy = false;                   // a unit struck another player's building
};

// One minute of `sim`, `twin` (when given) alongside; what happened noted in
// `progress`. True if the two came out identical.
bool Minute(ECS::Simulation& sim, ECS::Simulation* twin, int minute, Progress& progress);

// The rules' state for `player`: goals, shares, escrow, the town.
void Trace(const entt::registry& registry, int minute, Core::PlayerId player);

} // namespace Verify::AIGame
