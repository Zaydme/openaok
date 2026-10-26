#pragma once
#include <SDL2/SDL.h>

#include "Widgets.h"
#include "ecs/Simulation.h"

// The Achievements screen's Timeline page: a panel at (30, 35), 750 x 451,
// in game font 13, drawn from the players' histories (ecs/Timeline).
namespace Achievements {

// `art` is where the screen's 800x600 art is on the screen; the panel's
// positions are on that art.
void DrawTimeline(SDL_Renderer* r, const Screens::Theme& theme, const ECS::Simulation& sim,
                  SDL_Point art);

} // namespace Achievements
