#pragma once
#include <SDL2/SDL.h>

#include <string>

#include "Screens.h"
#include "Widgets.h"
#include "net/Lockstep.h"

// The main menu's Multiplayer menu, on the plank as Single Player's is: the
// connection type, Show Games and the games found, Join, Create and Restore.
// On the local network the games come of themselves; over TCP/IP, Show Games
// asks for the host's address in the Show Games dialog, with the games last
// joined beside it. Creating or joining waits on a message on the parchment
// until the lobby (screens/Setup) opens.
namespace Screens::Multiplayer {

void Init();

void OpenMenu();
void CloseMenu();
bool MenuOpen();

// Draws the menu over the town; the help for what the mouse is over, -1 for
// none of its.
void DrawMenu(SDL_Renderer* r, const Theme& town, const Pointer& p);
int HelpAt(const Pointer& p);
// An event for the menu or its dialog; false when it is not theirs.
bool MenuEvent(const SDL_Event& e, const Pointer& p, Request& request);

// Hosts the saved game at `path` again, as Restore chose it.
void Restore(const std::string& path);

// Finding games, and creating or joining one, each frame.
void Update(Request& request);
// The wait while a game is created or joined (Id::Multiplayer).
void DrawWait(SDL_Renderer* r, const Pointer& p);
void WaitEvent(const SDL_Event& e, const Pointer& p);

// The network game the lobby started, handed over to be played.
Net::Session TakeSession();

} // namespace Screens::Multiplayer
