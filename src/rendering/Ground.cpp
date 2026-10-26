#include "Ground.h"

#include <algorithm>
#include <cmath>

#include "core/IsometricMath.h"

namespace Rendering::Ground {

namespace {

int Level(const Physics::Grid& grid, int x, int y) {
    x = std::clamp(x, 0, grid.GetWidth() - 1);
    y = std::clamp(y, 0, grid.GetHeight() - 1);
    return grid.GetTile(x, y).elevation;
}

// How much each level of slope across a tile turns it toward or away from
// the light.
constexpr float kPerLevel = 0.5f;

} // namespace

float CornerLevel(const Physics::Grid& grid, int i, int j) {
    return 0.25f * static_cast<float>(Level(grid, i - 1, j - 1) + Level(grid, i, j - 1) +
                                      Level(grid, i - 1, j) + Level(grid, i, j));
}

float HeightPx(const Physics::Grid& grid, float wx, float wy) {
    const float fx = wx + 0.5f;
    const float fy = wy + 0.5f;
    const int i = static_cast<int>(std::floor(fx));
    const int j = static_cast<int>(std::floor(fy));
    const float u = fx - static_cast<float>(i);
    const float v = fy - static_cast<float>(j);
    const float top = CornerLevel(grid, i, j) * (1.0f - u) + CornerLevel(grid, i + 1, j) * u;
    const float bottom =
        CornerLevel(grid, i, j + 1) * (1.0f - u) + CornerLevel(grid, i + 1, j + 1) * u;
    return (top * (1.0f - v) + bottom * v) * kLevelPx;
}

float Light(const Physics::Grid& grid, int i, int j) {
    // The slope at the corner, from the four tiles round it. The upper left of
    // the screen is world -x, a little -y; ground rising that way faces away.
    const float a = static_cast<float>(Level(grid, i - 1, j - 1));
    const float b = static_cast<float>(Level(grid, i, j - 1));
    const float c = static_cast<float>(Level(grid, i - 1, j));
    const float d = static_cast<float>(Level(grid, i, j));
    const float along_x = 0.5f * ((b + d) - (a + c));
    const float along_y = 0.5f * ((c + d) - (a + b));
    const float facing = along_x + 0.5f * along_y;   // > 0: rises away from the light
    return std::clamp(-facing * kPerLevel, -1.0f, 1.0f);
}

std::pair<float, float> Pick(const Physics::Grid& grid, float screen_x, float screen_y) {
    // The ground drawn at a screen point stands higher on a hill, so the world
    // point under it is further down the screen than a flat map says. A few
    // rounds of correction settle it.
    auto world = Core::IsometricMath::ScreenToWorld(screen_x, screen_y);
    for (int round = 0; round < 3; ++round) {
        const float lift = HeightPx(grid, world.first, world.second);
        world = Core::IsometricMath::ScreenToWorld(screen_x, screen_y + lift);
    }
    return world;
}

} // namespace Rendering::Ground
