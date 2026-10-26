#pragma once
#include <cstdint>

#include "core/FixedPoint.h"

namespace ECS::Components {

// A unit that has been killed and is playing its death.
struct Dying {
    int32_t remaining_ms = 0;
};

// What a unit leaves on the ground: a body that lies for the dat's corpse
// decay time, or a carcass whose food rots away at the unit's own rate until
// it is gone.
struct Corpse {
    int32_t remaining_ms = 0;          // a body: until it disappears
    Core::Fixed32 rot_per_second{};    // a carcass: food lost each second
    Core::Fixed32 rot{};               // carried fraction of food not yet lost
};

} // namespace ECS::Components
