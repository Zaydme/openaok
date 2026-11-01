#pragma once

#include <string>

struct SDL_Window;

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

// Puts SDL's window in the app's UIWindowScene. iOS 27 requires the scene life
// cycle, and SDL 2 makes its window with the old application life cycle's
// initWithFrame:; the game's picture is not shown until it belongs to a scene.
void AttachWindowToScene(SDL_Window* window);

// The folder kept for the player's files of one kind ("Profile", "SaveGame",
// "Scenario"), under Documents, ending in a slash. The Files app shows it.
std::string PrefPath(const char* kind);

} // namespace HAL::iOS
