#pragma once
#include <SDL2/SDL.h>
#include <entt/entt.hpp>
#include <string>

#include "core/Types.h"

// The lines written over the left of the battlefield: what the other
// players say, the ages they reach, Wonders and relics, defeats, and the
// refusals ("Not enough wood.").
//
// Georgia 9 bold (12 px), white with the black outline of text over the
// world. Eight line panels 20 apart from 20 pixels into the map view, four in
// from its left, dropped 214 in a scenario with instructions (which take the
// top of the view). Lines do not move once written; each new one takes the
// next panel round. Each goes after a few seconds; the history stays for the
// chat review keys.
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

// A scenario's instructions are showing at the top of the view: the lines
// move down below them.
void SetScenarioInstructions(bool shown);

// A loaded game: what happened before it was saved is old news.
void CatchUp(const entt::registry& registry);

} // namespace Rendering::Messages
