#pragma once
#include <utility>

#include "physics/Grid.h"

// The shape of the ground, as it is drawn.
//
// The simulation keeps an elevation level per tile; drawing needs the ground
// as a surface. A tile corner's height is the average of the four tiles that
// meet there and a tile's middle is its own level, so a hill climbs smoothly
// rather than in steps. Terrain, fog, units and the mouse all go through
// here, so nothing drawn on a hillside floats off it or sinks into it.
namespace Rendering::Ground {

// Screen pixels per elevation level.
constexpr float kLevelPx = 12.0f;

// The height, in levels, of tile corner (i, j): world point (i - 0.5, j - 0.5).
float CornerLevel(const Physics::Grid& grid, int i, int j);

// The height of the ground under a world position, in screen pixels.
float HeightPx(const Physics::Grid& grid, float wx, float wy);

// How the light falls on the ground at tile corner (i, j): 0 for flat ground,
// up to +1 on a slope facing the light, down to -1 on one facing away. Light
// falls from the upper left of the screen.
float Light(const Physics::Grid& grid, int i, int j);

// The world position under a point of the screen (in world pixels, camera
// already added), allowing for the ground's height there.
std::pair<float, float> Pick(const Physics::Grid& grid, float screen_x, float screen_y);

} // namespace Rendering::Ground
