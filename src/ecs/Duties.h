#pragma once
#include <entt/entt.hpp>

#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Standing orders: patrol, guard and follow.
//
// Each is an order that keeps giving itself: a patrol walks to its far end and
// back for ever, and any enemy it meets on the way is fought by the unit's own
// stance (ecs/Combat) before the walk picks up again; a guard keeps within a
// couple of tiles of its charge and turns on anyone striking it; a follower
// keeps the same distance behind and fights for nobody. Any other order ends
// one -- ecs/Orders drops the duty with the rest of what a unit was doing.
namespace ECS::Duties {

void Patrol(entt::registry& registry, entt::entity unit, const Components::Vector2D& to);
void Guard(entt::registry& registry, entt::entity unit, entt::entity charge);
void Follow(entt::registry& registry, entt::entity unit, entt::entity charge);

// Once a tick, after orders and before movement.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Duties
