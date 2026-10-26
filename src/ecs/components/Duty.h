#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "ecs/components/Transform.h"

namespace ECS::Components {

// A standing order: one that goes on until another replaces it.
enum class DutyKind : uint8_t {
    Patrol,   // walk between two points, taking on what it meets
    Guard,    // keep by a unit and fight whoever attacks it
    Follow,   // keep behind a unit, never fighting for it
};

struct Duty {
    DutyKind kind = DutyKind::Patrol;
    Vector2D from{};          // a patrol's two ends
    Vector2D to{};
    bool outbound = true;     // heading for `to`
    entt::entity charge = entt::null;   // the unit guarded or followed
    int32_t replan_ms = 0;
};

} // namespace ECS::Components
