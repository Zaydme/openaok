#pragma once
#include "core/FixedPoint.h"

namespace ECS::Components {

struct Vector2D {
    Core::Fixed32 x;
    Core::Fixed32 y;
};

/**
 * Transform Component
 * Pure data struct holding logical grid position.
 */
struct Transform {
    Vector2D position;
    
    // Kept exclusively so the decoupled rendering loop can interpolate 
    // visual positions between ticks without touching logical state.
    Vector2D previous_position;
};

} // namespace ECS::Components
