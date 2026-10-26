#pragma once

#include "core/FixedPoint.h"

namespace ECS::Components {

struct Health {
    int current_hp = 100;
    int max_hp = 100;
    // Damage taken below a whole hit point, carried to the next blow. Hit
    // points are kept as fractions: a blow struck down a hill does a quarter
    // more, 3.75 from a villager's 3, and the three quarters count.
    Core::Fixed32 wound{};
};

} // namespace ECS::Components
