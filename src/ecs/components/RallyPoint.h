#pragma once
#include <entt/entt.hpp>

#include "ecs/components/Transform.h"

namespace ECS::Components {

// Where a building sends the units it trains: a point on the map, or
// something there -- a villager rallied onto a tree goes and cuts it.
struct RallyPoint {
    Vector2D at{};
    entt::entity target = entt::null;
};

} // namespace ECS::Components
