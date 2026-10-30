#pragma once
#include <cstdint>

#include "core/Types.h"

namespace ECS::Components {

// A member of a locked formation: chosen for a group of soldiers, they keep
// it until it is disbanded or another is chosen (ecs/Formations). Its members
// share `group`.
struct InFormation {
    uint32_t group = 0;
    Core::Formation formation = Core::Formation::Line;
};

} // namespace ECS::Components
