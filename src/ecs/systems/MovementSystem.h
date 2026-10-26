#pragma once
#include "physics/Grid.h"
#include <entt/entt.hpp>
#include <cstdint>

namespace ECS::Systems {

class MovementSystem {
public:
    // Takes the grid because movement respects it directly: the pathfinder
    // routes around obstacles, but avoidance can still push a unit sideways
    // into one.
    static void Update(entt::registry& registry, const Physics::Grid& grid,
                       int32_t fixed_dt_ms);
};

} // namespace ECS::Systems
