#pragma once
#include <SDL2/SDL.h>
#include <entt/entt.hpp>
#include <string>

#include "core/Types.h"

// The lines written over the left of the battlefield: what the other
// players say, the ages they reach, Wonders begun, finished and destroyed,
// relics, defeats, production halted for want of houses, the refusals ("Not
// enough wood."), and the notifications (rendering/Alerts).
//
// Georgia 9 bold (12 px), white with the black outline of text over the
// world. Eight line panels 20 apart from 20 pixels into the map view, four in
// from its left; in a single-player game 214 lower, below where a scenario's
// instructions go. The newest line is in the last panel, and each new one
// moves the others up one; a line moved out of the first is gone. Each goes
// ten seconds after it was written, and the same words written again while
// they still show go with them. The history stays for the chat review keys.
namespace Rendering::Messages {

void Reset();

// A line to show, in `colour` -- a player's own for what that player says.
void Post(const std::string& text, SDL_Color colour = SDL_Color{255, 255, 255, 255});

// What a player says, "Name: words", in that player's colour.
void Chat(const entt::registry& registry, Core::PlayerId from, const std::string& words);

// Turns the simulation's news since the last call into lines for `viewer`.
void Update(const entt::registry& registry, Core::PlayerId viewer);

void Draw(SDL_Renderer* renderer, int win_w, int win_h);

// Steps back through, or forward again through, lines already shown: the
// chat review keys.
void Review(int step);

// A single-player game: the lines sit lower, below the instructions' place.
void SetSinglePlayer(bool single);

// A loaded game: what happened before it was saved is old news.
void CatchUp(const entt::registry& registry);

} // namespace Rendering::Messages
