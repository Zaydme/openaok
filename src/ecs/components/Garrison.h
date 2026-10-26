#pragma once
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "ecs/components/Transform.h"

namespace ECS::Components {

// A building holding units: who is inside, in the order they came in.
struct Garrison {
    std::vector<entt::entity> inside;
    // The bell this Town Center rang is still ringing: rung again, it calls
    // everyone back to work.
    bool bell_rung = false;
};

// A unit on its way into a building, or aboard a transport ship.
struct Boarding {
    entt::entity building = entt::null;
    int attempts = 0;
    // Boarding a ship, which may still be on its way to the shore: how long
    // before the next try at reaching it.
    int32_t wait_ms = 0;
    // Sent by the town bell rather than by hand: it goes back to work when the
    // bell rings again.
    bool bell = false;
    entt::entity resume_resource = entt::null;
    entt::entity resume_building = entt::null;
};

// A transport ship ordered to put its load ashore: it sails to the shore
// nearest `at` and, there, everyone aboard steps onto the land.
struct Landing {
    Vector2D at{};
    bool routed = false;
    int attempts = 0;
};

// A unit inside a building. While it is, it has no Transform -- being in the
// world is having a place in it -- so nothing that looks at the world sees it:
// not the renderer, not sight, not movement, not anyone looking for a target.
struct Garrisoned {
    entt::entity building = entt::null;
    // Hit points healed and not yet whole.
    Core::Fixed32 heal{};
    bool bell = false;
    entt::entity resume_resource = entt::null;
    entt::entity resume_building = entt::null;
};

} // namespace ECS::Components
