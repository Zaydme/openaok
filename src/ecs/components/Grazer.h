#pragma once
#include <cstdint>

#include "ecs/components/Transform.h"

namespace ECS::Components {

// An animal that wanders about where it was put: a sheep, a deer.
struct Grazer {
    Vector2D home{};
    int32_t wait_ms = 0;     // until it next takes a step
    int32_t flee_ms = 0;     // still running from whoever struck it
};

} // namespace ECS::Components
