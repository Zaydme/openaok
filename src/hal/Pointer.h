#pragma once

#include <SDL2/SDL.h>

namespace HAL::Pointer {

// The system's mouse, moved and clicked from outside it: by the touch
// controls (hal/Touch) and the controller (hal/Pad). SDL keeps the pointer,
// the buttons and the double-click count as it does for a real mouse, so
// everything that reads the mouse works unchanged. Positions are window
// pixels; buttons are SDL's (SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT). On a machine
// with neither touch controls nor a controller, these do nothing.

// Moves the pointer; `by_finger` says a finger moved it, not a stick.
void Move(float x, float y, bool by_finger);
// A button going down or up where the pointer is.
void Press(Uint8 button);
void Release(Uint8 button);
// A click where the pointer is: down and straight back up.
void Click(Uint8 button);

// Where the pointer was last moved to, window pixels.
void Position(float& x, float& y);
// Whether a finger moved the pointer last. A finger leaves the pointer where
// it lifted, which may well be on an edge of the screen, so edge scrolling
// waits for a stick or a mouse to move it.
bool ByFinger();

} // namespace HAL::Pointer
