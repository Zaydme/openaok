#pragma once
#include "physics/Grid.h"
#include <cstdint>

#include <entt/entt.hpp>

#include "core/GameSetup.h"
#include "core/Types.h"

// How a game ends: standard victory.
//
// Conquest -- a player with nothing left standing, no unit and no building
// (walls and gates aside), is defeated, and when only one side remains it has
// won. A side is players allied each way who have all chosen allied victory
// (ecs/Diplomacy) -- team-mates from the lobby, or allies made in play; when
// any of them wins, the whole side wins. Under the standard rules a game is
// also won by a Wonder that stands for the lobby's number of years once
// finished, or by one side holding every relic on the map in its monasteries
// for as long.
//
// A year of game time is three seconds.
namespace ECS::Victory {

struct Outcome {
    bool over = false;
    uint16_t defeated = 0;   // a bit per seat
    uint16_t winners = 0;
    // Each seat's Wonder counting down, from when its first finished Wonder
    // stood: a bit per seat counting, and what each has left.
    uint16_t wonders = 0;
    int32_t wonder_ms[Core::kMaxPlayers] = {};
    // The relics counting down for the side holding them all: which of its
    // players, and what is left.
    Core::PlayerId relic_holder = 0;
    int32_t relic_ms = 0;

    bool Defeated(Core::PlayerId p) const { return (defeated >> p) & 1u; }
    bool Won(Core::PlayerId p) const { return (winners >> p) & 1u; }
    bool CountingWonder(Core::PlayerId p) const { return (wonders >> p) & 1u; }
};

// A scenario's own goal, from its Global Victory settings: the score a
// score game is won at; the time a time limit game ends at, in tenths of a
// year; and a custom game's conditions, every one of them or any (`all`):
// conquest, `relics` held by a side, `explored` percent of the map by any of
// its players (each 0 for none).
struct Goal {
    int32_t score = 0;
    int32_t time = 0;
    bool conquest = true;
    int32_t relics = 0;
    int32_t explored = 0;
    bool all = false;
};

// The module's state in the registry's context. Here rather than in the
// source so the save module can write it out.
struct State {
    Outcome outcome;
    Core::GameSetup setup;
    Goal goal;
    int32_t check_ms = 0;
    int wonder_id = -1;
    int relic_id = -1;
    int king_id = -1;
};

void Setup(entt::registry& registry, const Core::GameSetup& setup);

// A scenario's goal (ScenarioWorld), after Setup.
void SetGoal(entt::registry& registry, const Goal& goal);

// Once a tick. In a Regicide game a player whose King is dead -- or is not
// its own any more -- is out, everything it has destroyed as a resigning
// player's is.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

// The first work done on a building site: a Wonder's ground is shown to
// every player from then on, so all of them see it go up.
void Begun(entt::registry& registry, entt::entity site);

// `player`'s side wins at once, everyone else losing ("i r winner").
void Win(entt::registry& registry, Core::PlayerId player);

// Whether `player` has a living King of its own, in the world or sheltering.
bool HasKing(const entt::registry& registry, Core::PlayerId player);

// The outcome so far; null if the world was built without one.
const Outcome* Read(const entt::registry& registry);

// `player` gives up: out of the game at once, and everything it owns
// destroyed.
void Resign(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player);

// A scenario's Declare Victory (ecs/Triggers): the players in
// `winners` (a bit per seat) and whoever shares their victory win, every
// other player still in the game is defeated, and the game is over. One
// trigger may declare several winners; they are gathered and passed at once.
void Declare(entt::registry& registry, uint16_t winners);

} // namespace ECS::Victory
