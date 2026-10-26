#pragma once
#include <SDL2/SDL.h>

#include <entt/entt.hpp>

// The technology tree screen (the "One Button Tech Tree Screen"):
// the whole tree on one horizontal strip of parchment that scrolls -- the left
// panel with the civilisation drop-down, the civ's description and the colour
// legend; the age shields with their bands; every building with its units and
// technologies hanging from it in boxes coloured by kind and status, joined by
// lines; the scroll arrows and Close along the bottom.
//
// Opened from the game setup screen for the civ chosen there, or in a game
// (F2, the top bar's button) for the player's own civ, where what the player
// has researched shows lit and the shield of the player's age glows. The
// layout is TechTreeLayout's; the art is interfac.drs's technode, techback,
// techages and arrows, in the game's palette.
namespace Screens::TechTree {

// Before a game, from the setup screen: `civ` as the lobby has it (1-13).
void OpenPregame(int civ);
// In a game: the local player's civ and statuses, read from the registry on
// the first frame drawn.
void OpenInGame();
bool IsOpen();
void Close();

enum class Result {
    Ignored,   // not the screen's
    Taken,     // the screen used it
    Closed,    // Close, Escape or the tech tree key: the screen is gone
};

// One input event, in the frame's pixels. `w` and `h` are the screen size.
Result HandleEvent(const SDL_Event& event, int w, int h);

// Draws the whole screen at w x h. `registry` is the game's in a game, null
// before one.
void Draw(SDL_Renderer* renderer, const entt::registry* registry, int w, int h);

} // namespace Screens::TechTree
