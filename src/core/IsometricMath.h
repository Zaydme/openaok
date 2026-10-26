#pragma once
#include "core/FixedPoint.h"
#include <SDL2/SDL.h>
#include <utility>

namespace Core {

class IsometricMath {
public:
    // Tile size in pixels (2:1).
    static constexpr int TILE_WIDTH = 96;
    static constexpr int TILE_HEIGHT = 48;

    /**
     * Converts from deterministic logical grid position to screen pixels.
     * X increases down-right, Y increases down-left.
     */
    static SDL_FPoint WorldToScreen(Fixed32 logical_x, Fixed32 logical_y) {
        float lx = logical_x.ToFloat();
        float ly = logical_y.ToFloat();
        
        float sx = (lx - ly) * (TILE_WIDTH / 2.0f);
        float sy = (lx + ly) * (TILE_HEIGHT / 2.0f);
        
        return {sx, sy};
    }

    /**
     * Converts from screen pixels back to logical grid (useful for mouse picking).
     */
    static std::pair<float, float> ScreenToWorld(float screen_x, float screen_y) {
        float lx = (screen_x / (TILE_WIDTH / 2.0f) + screen_y / (TILE_HEIGHT / 2.0f)) / 2.0f;
        float ly = (screen_y / (TILE_HEIGHT / 2.0f) - screen_x / (TILE_WIDTH / 2.0f)) / 2.0f;
        return {lx, ly};
    }
};

} // namespace Core
