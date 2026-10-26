#pragma once
#include <SDL2/SDL.h>

#include "core/Types.h"
#include "ecs/Simulation.h"

// Flares on the minimap, a flashing X. Read from the simulation's flare log
// (ecs/Flares), for whoever is sitting at this machine.
namespace Rendering::Signals {

// Draws every flare the viewer is shown and that is still up, as a 9-pixel X
// in pale yellow and white by turns, whoever sent it, on the minimap well --
// the last thing the minimap draws of the world.
void DrawOnMinimap(ECS::Simulation& sim, SDL_Renderer* sdl_r, const SDL_FRect& well,
                   Core::PlayerId viewer);

} // namespace Rendering::Signals
