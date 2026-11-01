#pragma once

#include <string>

namespace HAL::iOS {

// The game's data in the app's Documents folder, before SDL starts: the folder
// OpenAoK-data, made on a computer by ios/package.py, is looked for there. On
// first start, or when the data is of another version than this build plays
// from, the system's document picker asks for the folder and it is copied in,
// checked against its manifest as it goes. The game is then played from the
// folder's build/, so every "../assets/..." finds its file, as on a desktop.
//
// The setup screen stays up until the data is there: this returns false only
// when the folder cannot be entered after it is.
bool Start(int argc, char* argv[]);

// The folder kept for the player's files of one kind ("Profile", "SaveGame",
// "Scenario"), under Documents, ending in a slash. The Files app shows it.
std::string PrefPath(const char* kind);

} // namespace HAL::iOS
