#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "physics/Grid.h"

// Changing sides: a unit or building passing from one player to another --
// converted by a monk, or a herdable claimed, or one of Gaia's taken by the
// first player to see it.
//
// Everything it contributes to its owner (a villager's place in the
// population, a House's headroom) moves with it. Whatever it was doing stops,
// and a building turns its garrison out, since the units inside are not
// converted with it.
namespace ECS::Allegiance {

void Change(entt::registry& registry, const Physics::Grid& grid, entt::entity entity,
            Core::PlayerId player);

// Whether one of `unit_id`'s, made Gaia's, is taken by the first player to
// see it: anything but what the dat classes as an animal, a bird, a relic or
// artifact, a heap, terrain or a miscellaneous object.
bool Takeable(int unit_id);

// Once a tick, after the players' sight: each of Gaia's that can be taken
// goes to the lowest-numbered player who sees it, and stays theirs.
void Capture(entt::registry& registry, const Physics::Grid& grid);

} // namespace ECS::Allegiance
