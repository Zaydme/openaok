#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Transform.h"

// Bringing a unit into the world.
//
// A unit is a dozen components that all have to agree -- its identity, its
// graphics, its hit points and sight from the dat, who owns it, whether it is
// a villager that can work. The starting villagers and every unit a building
// trains need exactly the same set, so it is assembled here once.
namespace ECS::Units {

// The dat's class for a villager: the class that does economic work.
constexpr int kVillagerClass = 4;

// Creates a unit of `unit_id` owned by `player`, standing at `at`, with every
// component the systems and interface expect. Returns null if the unit is not
// one the data knows.
entt::entity Spawn(entt::registry& registry, int unit_id, Core::PlayerId player,
                   Components::Vector2D at);

// Turns `unit` into another of the dat's units -- a monk picking up a relic
// becomes the monk carrying one, and back -- keeping its owner, its place and
// its wounds in proportion. Not an upgrade: an upgrade keeps the identity and
// changes the owner's rules for it; this changes what the unit is.
void Become(entt::registry& registry, entt::entity unit, int unit_id);

} // namespace ECS::Units
