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
    // A Wonder or the relics counting down: whose, and what is left.
    Core::PlayerId wonder_holder = 0;
    int32_t wonder_ms = 0;
    Core::PlayerId relic_holder = 0;
    int32_t relic_ms = 0;

    bool Defeated(Core::PlayerId p) const { return (defeated >> p) & 1u; }
    bool Won(Core::PlayerId p) const { return (winners >> p) & 1u; }
};

// The module's state in the registry's context. Here rather than in the
// source so the save module can write it out.
struct State {
    Outcome outcome;
    Core::GameSetup setup;
    int32_t check_ms = 0;
    int wonder_id = -1;
    int relic_id = -1;
    int king_id = -1;
};

void Setup(entt::registry& registry, const Core::GameSetup& setup);

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
