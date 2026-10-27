#pragma once

#include <string>

namespace HAL::Platform {

// What differs between the machines the game runs on, beyond what SDL covers.

// Before anything else. On the Switch: refuses to run from the Album, which
// leaves the game too little memory, or without the game's data beside the
// .nro, saying why on the screen; sends what the engine prints to nxlink when
// it launched the game, and otherwise to openaok.log beside the .nro; and
// enters game/build there, laid out as the source tree is, so every
// "../assets/..." finds its file. False when the game must not go on.
// Elsewhere it does nothing.
bool Start(int argc, char* argv[]);

// Whether the world's art is decoded before the first frame, rather than as
// it first comes into view (it is, but on the Switch).
bool WarmArt();

// What the game has in memory and may have, in the log (Switch).
void LogMemory(const char* when);

// The folder kept for the player's files of one kind ("Profile", "SaveGame",
// "Scenario"), ending in a slash; empty when there is none.
std::string PrefPath(const char* kind);

} // namespace HAL::Platform
