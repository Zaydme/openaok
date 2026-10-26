#pragma once

#include <SDL2/SDL.h>

namespace HAL::Touch {

// Touch controls played as a mouse: the fingers are turned into the mouse SDL
// already reports, so everything that reads the mouse works unchanged.
//
//   tap                   a left click where the finger went down
//   hold still a moment   a right click there; the phone ticks to say so
//   drag one finger       a left drag: a box round units, a wall laid out
//   drag two fingers      the map, moving under them
//
// While something is being aimed -- a building waiting to be placed, an order
// waiting for its target -- the finger moves the pointer instead and lifting
// it clicks there, so a building can be seen and slid into place first.

// Turns the controls on, before SDL starts (Android): SDL stops making its own
// mouse out of the first finger. Left off, every call here does nothing.
void Enable();
bool Enabled();

// A finger, or the controls' hold timer going off: true when the event was the
// controls' and has been dealt with.
bool Handle(const SDL_Event& event);

// How far two fingers have dragged the map since the last call, in the frame's
// pixels.
void TakePan(float& dx, float& dy);

// Set once a frame by the game: whether the next touch aims.
void SetAiming(bool aiming);

// Whether a mouse is plugged in, as the system tells it (Android); a machine
// without the controls is taken to have one. Edge scrolling needs a real mouse:
// a finger leaves the pointer where it lifted, which may well be on an edge.
bool MouseAttached();

} // namespace HAL::Touch
