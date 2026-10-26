#pragma once
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

    // Which way this unit committed to going around an obstruction. Once
    // picked, the side is held until the obstruction clears, so the unit does
    // not flip sides as the geometry shifts.
    int8_t avoid_side = 0;

    // Ticks since the unit last got closer to the waypoint it is walking to --
    // the waypoint, not the goal, and not distance travelled: a unit walking
    // round a building moves away from its goal, and one pinned by a crowd
    // slides back and forth without going anywhere. Between corners the route
    // is a straight line, so getting no closer to the next corner is being
    // stuck.
    int16_t blocked_ticks = 0;
    // The closest the unit has come to its current waypoint, and which
    // waypoint that was measured for.
    Core::Fixed32 closest{};
    size_t closest_for = static_cast<size_t>(-1);
    // How many times the route has been rebuilt for this order. Bounded, so a
    // genuinely unreachable goal is abandoned instead of recomputed forever.
    int8_t repaths = 0;
};

} // namespace ECS::Components
