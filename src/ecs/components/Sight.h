#pragma once

namespace ECS::Components {

struct Sight {
    int radius = 6;
    // Seeing only the ground under itself: a building not yet finished.
    bool footprint = false;
};

} // namespace ECS::Components
