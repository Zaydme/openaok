#pragma once
#include <entt/entt.hpp>

#include "physics/Grid.h"

// What happens when something gatherable runs out.
//
// The dat says what each leaves behind -- a tree its stump, a farm the dead
// farm -- and whatever stood on the ground stops blocking it. A farm is torn
// down as a building.
namespace ECS::Resources {

// `resource` has been gathered out: replace it with its remains.
void Exhaust(entt::registry& registry, Physics::Grid& grid, entt::entity resource);

} // namespace ECS::Resources
