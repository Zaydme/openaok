#pragma once
#include <SDL2/SDL.h>

namespace Rendering {

class Renderer;

// Sprites drawn through a dither pattern: a pattern of screen pixels, fixed
// to the frame like the fog's checkerboard (FogRenderer.h), where the
// sprite's own pixel gives way to one colour. A building being placed is
// drawn through two of them (Placement.h).
//
// Use: Begin, then draw -- at the scale in force, in back-to-front order --
// each sprite that takes the pattern, any colour, and wipe with
// FogRenderer::EraseBlend() wherever a sprite that does not stands in front;
// End turns that coverage into the colour the pixels give way to, keeps the
// pattern's pixels of it and lays it over the frame, which already holds the
// sprites drawn plain.
namespace Stipple {

enum class Pattern {
    // One pixel in four: x % 4 == 3 on the fog's odd rows and x % 4 == 1 on
    // its even rows.
    Quarter,
    // Half the pixels, those the fog's checkerboard leaves alone -- x + y odd.
    Half,
};

// False if the layer cannot be had; End must not be called then.
bool Begin(Renderer& renderer, SDL_Renderer* sdl_r);
void End(SDL_Renderer* sdl_r, Pattern pattern, SDL_Color colour);

} // namespace Stipple

} // namespace Rendering
