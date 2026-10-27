#pragma once

#include <cstdint>

namespace HAL::TextInput {

// Typed text. SDL sends characters only while text input is on, and the
// game's keys are hotkeys otherwise, so the screen in front says what it
// takes whenever that changes.
//
// On the Switch, text comes from the system's keyboard instead, which covers
// the game while it is up: a line to type gets it straight away, and what is
// typed on it arrives as though typed, followed by Enter; backing out of it is
// Escape. Text fields that are always on a screen get it when the player asks
// (the controller's -).
enum class Kind : uint8_t {
    None,    // nothing to type: every key is a hotkey
    Fields,  // text fields that are always there: a name, the editor's
    Line,    // a line being typed now: chat, a saved game's name
};
void Want(Kind kind);

// Brings up the system's keyboard for the screen's fields: false when the
// screen has none, or the machine has no such keyboard.
bool ShowForFields();

// Once a frame: on the Switch, brings up the keyboard when it is due.
void Update();

} // namespace HAL::TextInput
