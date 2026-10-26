#pragma once
#include <entt/entt.hpp>
#include <cstdint>

#include "physics/Grid.h"

namespace ECS::Systems {

class WorkerSystem {
public:
    static void Update(entt::registry& registry, Physics::Grid& grid, int32_t fixed_dt_ms);
};

} // namespace ECS::Systems
