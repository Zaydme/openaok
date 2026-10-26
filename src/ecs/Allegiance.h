#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "physics/Grid.h"

// Changing sides: a unit or building passing from one player to another --
// converted by a monk, or a herdable claimed.
//
// Everything it contributes to its owner (a villager's place in the
// population, a House's headroom) moves with it. Whatever it was doing stops,
// and a building turns its garrison out, since the units inside are not
// converted with it.
namespace ECS::Allegiance {

void Change(entt::registry& registry, const Physics::Grid& grid, entt::entity entity,
            Core::PlayerId player);

} // namespace ECS::Allegiance
