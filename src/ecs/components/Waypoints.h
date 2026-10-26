#pragma once
#include <array>
#include <cstdint>

#include "core/FixedPoint.h"
#include "ecs/components/Transform.h"

namespace ECS::Components {

// Where a unit is to walk on to, in order, once it reaches the point it is
// walking to now: waypoints, laid with Shift and the right button. Any other
// order clears them.
struct Waypoints {
    // How many a unit keeps.
    static constexpr int kMax = 10;
    std::array<Vector2D, kMax> points{};
    uint8_t count = 0;
    // A group's pace, its slowest member's, for every leg; zero for none.
    Core::Fixed32 speed_cap{};
};

} // namespace ECS::Components
