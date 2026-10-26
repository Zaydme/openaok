#pragma once
#include <cstdint>

#include "core/FixedPoint.h"

namespace ECS::Components {

// What stage of its life a building is in: a site is a hole in the ground
// that villagers work on and that shows the construction graphic, and a
// finished building is the real structure.
// Which one it is decides its art, whether it can be worked on, and what the
// interface offers for it.
enum class BuildingState : uint8_t {
    UnderConstruction,
    Complete,
};

// Marks an entity as a building. Which building it is lives in Identity, the
// dat unit id, like every other entity.
struct Building {
    BuildingState state = BuildingState::Complete;
    // How far a site is through its construction, from 0 to 1. Kept apart
    // from hit points because the two diverge: a site that is attacked while
    // it goes up loses hit points without losing the work done on it.
    Core::Fixed32 progress{};
};

} // namespace ECS::Components
