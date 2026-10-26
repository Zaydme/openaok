#pragma once
#include "core/Types.h"

namespace ECS::Components {

struct Resource {
    Core::ResourceType type = Core::ResourceType::Wood;
    int current_amount = 100;
};

} // namespace ECS::Components
