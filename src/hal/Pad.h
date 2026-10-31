#pragma once

#include <SDL2/SDL.h>

namespace HAL::Pad {

// The controller played as a mouse and the game's keys (Switch):
//
//   left stick          the pointer, the faster the further it is pushed
//   right stick         the map
//   A                   the left button; held, a drag: a box, a wall
//   B                   a right click
//   X                   the next idle villager            .
//   Y                   Escape: backs out of whatever is half done
//   L                   the Town Center                    H
//   R                   the next idle military unit        ,
//   ZL, held            Shift: adds to a selection, queues orders, trains five
//   ZR                  the last notification              Home
//   +                   the game menu                      F10
//   -                   chat                               Enter
//                       (on a screen with a name to type, the keyboard)
//   D-pad up            pause                              F3
//   D-pad down          the selected unit                  Space
//   D-pad left, right   slower, faster                     keypad - and +
//
// The keys are the game's default hotkeys, pressed as though typed.

// Turns the controls on. Left off, every call here does nothing.
void Enable();
bool Enabled();

// A controller's button: true when the event was the controls' and has been
// dealt with.
bool Handle(const SDL_Event& event);

// Once a frame: the left stick moves the pointer.
void Update();

// How far the right stick is pushed, -1 to 1 each way, for the map to scroll
// as it does under the arrow keys; nothing while ZL is held.
void Scroll(float& x, float& y);

// With ZL held, how far the right stick is pushed up (to 1, zooming in) or
// down (to -1, zooming out); 0 otherwise.
float Zoom();

// A short buzz of the controller.
void Rumble();

} // namespace HAL::Pad
