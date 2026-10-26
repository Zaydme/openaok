#pragma once

namespace ECS::Components {

// A gate in a wall: its own tiles are open to its owner and allies and shut to
// everyone else, unless it is locked, when they are shut to all (the grid
// keeps that, tile by tile -- physics/Grid). Here is what the gate itself
// shows: it is drawn closed (GTAA2), with the transform unit's art, the
// portcullis raised (GTAB2), swapped in while a unit it opens for is at it.
struct Gate {
    bool locked = false;
    bool open = false;
    // The arch's art, shut and raised: the stack unit's own graphic and its
    // transform unit's, resolved when the gate is finished.
    int closed_slp = -1;
    int open_slp = -1;
};

} // namespace ECS::Components
