#pragma once
#include <entt/entt.hpp>

#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Death, and what it leaves behind.
//
// A building that dies comes down through its own module, leaving its rubble.
// A unit stops being part of anything at once -- it gives back its place in its
// owner's population, cannot be selected, and fights and works no more --
// and plays its dying graphic. Then it becomes the dat's dead unit: a body that
// lies for the dat's corpse decay time, or, for an animal, a carcass holding
// the food it had, which rots away at the animal's own rate. A hunter that was
// after the animal goes on to the carcass.
namespace ECS::Mortality {

// `victim` has no hit points left.
void Kill(entt::registry& registry, Physics::Grid& grid, entt::entity victim);

// Plays the dat's `graphic` once at `at` and is gone: a building's fall in
// dust, a stone's burst where it lands. Its sound plays as it starts
// (audio/WorldSounds). Null if the data has no such graphic.
entt::entity PlayOnce(entt::registry& registry, int graphic, Components::Vector2D at);

// Advances the dying and the dead by one tick.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Mortality
