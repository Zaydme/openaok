#pragma once
#include <entt/entt.hpp>
#include <cstdint>
#include <string>

#include "ecs/components/Animation.h"

namespace ECS::Systems {

class AnimationSystem {
public:
    static void Update(entt::registry& registry, int32_t fixed_dt_ms);

    // Fills an animation set from one dat unit's graphics: which SLP each
    // state draws, how many frames it has, and how long a frame lasts. Used
    // by spawning and by a villager changing job; the list of dat fields it
    // reads must match `AnimState`.
    static void LoadSet(Components::Animation& anim, const std::string& unit_name);
};

} // namespace ECS::Systems
