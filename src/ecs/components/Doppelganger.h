#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Transform.h"

namespace ECS::Components {

// One piece of the picture a player keeps of something it saw and no longer
// sees: a building as it stood when it went out of sight, a mine or a tree as
// it was before it went while nobody of the player's looked. Drawn for that
// player alone, in the colour of whose it was, until the player sees its
// ground again (ecs/Fog).
struct Doppelganger {
    Core::PlayerId player = 0;          // who remembers it
    Core::PlayerId colour = 0;          // whose it was, for its colour; 0 for Gaia's
    entt::entity source = entt::null;   // what it shows, while that lasts
    int unit_id = -1;                   // the thing's unit: its footprint, its minimap square
    Vector2D origin{};                  // where the thing stands
    bool main = false;                  // the piece the minimap marks
};

// On something a player keeps a picture of: those players, a bit each, who see
// the picture in the fog rather than the thing.
struct Remembered {
    uint16_t by = 0;
};

} // namespace ECS::Components
