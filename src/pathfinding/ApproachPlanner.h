#pragma once
#include <cstddef>

#include "core/FixedPoint.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Choosing where to stand in order to work on something. The caller hands
// over the limits of the ground and its own position.
//
// Each caller asks for itself, from where it is, and the planner returns the
// nearest free spot facing it; a crowd sorts itself out locally.
namespace Pathfinding {

struct ApproachRequest {
    // The ground being approached, as its owner reports it.
    ECS::Buildings::Bounds target;
    // Where the caller is standing now.
    ECS::Components::Vector2D from;
    // How far outside `target` to stand: the worker's reach plus its own
    // half-width, so its middle is not asked to occupy the wall.
    Core::Fixed32 standoff;
    // How much room each worker at the target needs. Also the step the search
    // takes along the wall.
    Core::Fixed32 clearance;
    // Spots already spoken for: where workers already at work here are
    // standing, and where those still walking are *headed*. A walker counts by
    // its destination, never its position, so the answer stays stable.
    const ECS::Components::Vector2D* taken = nullptr;
    size_t taken_count = 0;
    // How near each of `taken` a spot may be, when they differ: for a body
    // standing there, its radius and the caller's together. Null for
    // `clearance` for all.
    const Core::Fixed32* taken_room = nullptr;
    // Whose ground the spot has to be: land for a villager, the sea for a
    // ship come to a dock (physics/Grid's layers).
    Physics::Layer layer = Physics::kLand;
};

struct ApproachResult {
    // False when the whole edge is blocked or taken. The caller starts work
    // only on a true.
    bool found = false;
    // Not found because the open ground there is all spoken for: a spot will
    // come free, where a blocked edge never will.
    bool full = false;
    ECS::Components::Vector2D spot{};
};

// The spot on `target`'s edge for this caller to stand on.
//
// Picks the face the caller is in front of, starts from the point on that face
// nearest the caller (kept clear of the corners) and steps outward along the
// edge, alternating sides, until it finds ground that is walkable and clear of
// everyone already spoken for. The caller's own face is searched finely and in
// full before anywhere else, so a crowd fills the wall in front of it before
// anyone is sent round a corner.
ApproachResult FindStandingSpot(const Physics::Grid& grid,
                                const ApproachRequest& request);

} // namespace Pathfinding
