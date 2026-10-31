#pragma once
#include <entt/entt.hpp>

#include "physics/Grid.h"

// What animals do of their own accord, from their task lists.
//
// A Graze task makes an animal wander a little about where it was put; a
// sheep's "get auto-converted" task makes it a herdable, which joins whichever
// player's unit comes within its sight while none of its owner's is there --
// a Gaia sheep the first scout to find it. Prey -- a grazer with no attack, a
// deer -- runs from whoever strikes it. Wolves and boar are armed and fight
// (ecs/Combat); this is everything else.
namespace ECS::Animals {

// Whether the entity's tasks make it a herdable.
bool Herdable(const entt::registry& registry, entt::entity entity);

// Whether it is prey: a grazer with no attack of its own.
bool Prey(const entt::registry& registry, entt::entity entity);

// Prey struck by `attacker` runs from it.
void Flee(entt::registry& registry, const Physics::Grid& grid, entt::entity animal,
          entt::entity attacker);

// Once a tick: herdables change hands, grazers wander, the fleeing stop.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

// Something has become another of the dat's units in place (Units::Become):
// the next tick looks again for grazers not yet grazing.
void Changed(entt::registry& registry);

} // namespace ECS::Animals
