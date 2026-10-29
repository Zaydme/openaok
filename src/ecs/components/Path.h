#pragma once
#include <entt/entt.hpp>

#include "ecs/components/Transform.h"
#include <vector>

namespace ECS::Components {

struct Path {
    std::vector<Vector2D> waypoints;
    size_t current_waypoint_index = 0;

    // Where the order was actually for. The waypoints are a route to it and
    // get thrown away and rebuilt when the route stops working, so the
    // destination has to be remembered separately.
    Vector2D goal{};
    bool has_goal = false;

    // A cap on speed, in tiles per second, for a unit marching with others:
    // a formation moves at its slowest member's pace. Zero: no cap.
    Core::Fixed32 speed_cap{};

    // What the walk is to, if anything, and how near it is near enough: the
    // walk ends as soon as the unit's box is within `range` of the target's
    // (Combat::BoxOf), wherever it has got to -- as the original's walks end.
    entt::entity target = entt::null;
    Core::Fixed32 range{};

    // Times the route has been planned again on the way, for ground that has
    // closed across it -- a building put up, a gate shut. Bounded, so a way
    // that has gone for good is given up.
    int8_t replans = 0;
};

} // namespace ECS::Components
