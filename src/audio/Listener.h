#pragma once
#include "core/FixedPoint.h"

// Where the player is listening from: the view on screen.
//
// Sounds belong to what the player is looking at -- a fight off screen is
// silent, one at the edge of the view is quiet and to that side. A
// listener is the view's rectangle in world pixels; a sound's volume and pan
// come from where its source is drawn relative to it.
namespace Audio {

struct Listener {
    float left = 0.0f;     // the view's top-left, in unzoomed world pixels
    float top = 0.0f;
    float width = 1.0f;    // its size in the same pixels
    float height = 1.0f;
};

struct Placement {
    float volume = 0.0f;   // 0: inaudible
    float pan = 0.0f;      // -1 left .. 1 right
};

// How a sound at world position (x, y) is heard: full in the middle of the
// view, a little softer toward its edges, fading out just beyond them.
Placement Place(const Listener& listener, Core::Fixed32 x, Core::Fixed32 y);

} // namespace Audio
