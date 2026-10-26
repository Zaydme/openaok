#pragma once
#include <cstdint>

#include <entt/entt.hpp>

namespace ECS::Components {

// A siege engine packing up or setting up (ecs/Packing): what it becomes,
// how long is left, and what it was sent at, to open fire on once set up.
struct Transforming {
    int to_unit = -1;
    int32_t left_ms = 0;
    int32_t total_ms = 0;
    bool packing = false;
    entt::entity then_attack = entt::null;
};

// A packed engine on its way to within range of `target`, to set up there.
struct Assaulting {
    entt::entity target = entt::null;
    int32_t replan_ms = 0;
    int failures = 0;
};

} // namespace ECS::Components
