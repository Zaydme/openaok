#pragma once

namespace ECS::Components {

// A unit whose graphic is a composite: the dat draws it as a list of other
// graphics, each at an offset. A galley is its hull, its sails and the wake it
// sits in -- three graphics, sixteen facings each -- and the graphic its unit
// names has no art of its own at all. Resolved from the dat's deltas once,
// when the animation changes, so the renderer only indexes atlases.
//
// Present only while the graphic shown is a composite; a plain sprite has
// none.
struct SpritePiece {
    int slp_id = -1;
    int frames_per_angle = 1;
    // Facings the graphic was drawn for (8, or 16 for a ship's), of which
    // the first half and one more are stored and the rest are mirrors.
    int angles = 8;
    int offset_x = 0;   // screen pixels from the unit's anchor
    int offset_y = 0;
    // Draw order among the pieces: the dat's layer (a wake at 10 under a hull
    // at 20), then list order.
    int bias = 0;
};

struct SpritePieces {
    static constexpr int kMax = 8;
    SpritePiece piece[kMax];
    int count = 0;
};

} // namespace ECS::Components
