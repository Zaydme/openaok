#pragma once
#include <entt/entt.hpp>

#include "core/GameSetup.h"
#include "core/Types.h"

// The Castle's spying: Spies in most games, Treason in Regicide.
//
// Spies is a technology (Techs::Spies names it). Its price is the dat's, 200
// gold, for each villager of the players in the game its researcher is not
// allied with each way -- never fewer than one, never more than 150 -- times
// the player's research rate (dat resource 85) where it has one, and it
// follows them as they come and go. Researched, it shows the player what
// every player it is not allied with sees (ecs/Vision).
//
// A Regicide game has Treason in its place: not researched but done at once
// at a Castle, 400 gold each time, as often as the gold lasts. Each King of a
// player it does not regard as an ally is marked for it and its allies with
// the dat's FLARE4 (ecs/Flares): a flare on the minimap that sees two tiles
// round the King, ten seconds. Death Match has neither.
namespace ECS::Spies {

// Takes Spies away from every player where the game does not have it:
// Regicide and Death Match.
void Setup(entt::registry& registry, const Core::GameSetup& setup);

// Once a tick: each player's price for Spies, from the villagers it counts.
void Update(entt::registry& registry);

// Whether `player` can do Treason now: a Regicide game, and the gold.
bool CanTreason(const entt::registry& registry, Core::PlayerId player);

// Treason at `castle`, a finished Castle of the player's. False if it cannot
// be done.
bool Treason(entt::registry& registry, entt::entity castle, Core::PlayerId player);

} // namespace ECS::Spies
