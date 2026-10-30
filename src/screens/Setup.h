#pragma once
#include <SDL2/SDL.h>

#include "Screens.h"
#include "Widgets.h"
#include "net/Lockstep.h"

// The game setup screen, on the parchment with the wooden chest (scr3,
// 50053): the players on the left, the game's settings on the right, Start
// Game and Cancel below.
//
// Single player it sets up a Random Map, Regicide or Death Match game against
// the computer. In a network game it is the lobby: its eight rows as the
// host's table has them (net/Lobby), each with its ping; chat; the I'm Ready!
// check box and the IP button. The host sets the game in the settings inset
// and starts it; everyone else sees the settings as lines of text and waits.
namespace Screens::Setup {

void Init();

// Random Map (0), Regicide (1) or Death Match (2), against the computer.
void OpenSingle(int kind);
// The lobby of `session`, which outlives the screen.
void OpenLobby(Net::Session& session);

// The lobby's part each frame: its messages, and the game once started.
void Update(Request& request);

void Draw(SDL_Renderer* r, const Pointer& p);
void HandleEvent(const SDL_Event& e, const Pointer& p, Request& request);

} // namespace Screens::Setup
