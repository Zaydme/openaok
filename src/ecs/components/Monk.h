#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"

namespace ECS::Components {

// A unit whose tasks convert or heal: its faith, and what it is working on.
struct Monk {
    Core::Fixed32 faith{100};
    entt::entity target = entt::null;
    bool converting = false;        // else healing
    bool ordered = false;           // sent at it, rather than finding it
    int32_t elapsed_ms = 0;         // at it, in range
    int32_t chase_ms = 0;           // until the walk to it is planned again
    int32_t scan_ms = 0;            // until it next looks for someone to heal
    int32_t roll_ms = 0;            // until its next try at a conversion
    // Chanting this tick: the animation shows the conversion graphic.
    bool chanting = false;
};

} // namespace ECS::Components
