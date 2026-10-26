#pragma once
#include <SDL2/SDL.h>
#include "ecs/Simulation.h"
#include <string>
#include <cstdint>
#include <unordered_map>

namespace Rendering {

class Renderer;

struct TerrainChunk {
    SDL_Texture* texture = nullptr;
    int grid_x = 0;
    int grid_y = 0;
    SDL_FRect screen_bounds{0, 0, 0, 0};
    uint64_t last_used = 0;   // frame counter, for eviction
};

class TerrainRenderer {
public:
    static void DrawWaterLayer(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r);
    static void DrawTerrain(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r);

    // One tile of ground: where it sits inside its terrain's atlas. The whole
    // terrain set is a single texture, so a chunk bakes without ever swapping
    // texture, and picking a variant is rect arithmetic.
    struct TerrainTile {
        SDL_Texture* atlas = nullptr;
        SDL_Rect src{0, 0, 0, 0};
    };
    static TerrainTile GetTerrainTile(Core::TerrainType type, uint8_t variant);
    
    // Tiles per chunk side; small, so little texture goes to the empty
    // corners of each chunk's isometric bounding box.
    static const int CHUNK_SIZE = 8;
    // Live chunks, keyed by chunk coordinate. Only what the viewport needs is
    // ever baked or kept.
    static std::unordered_map<uint64_t, TerrainChunk> chunks;
    
    static void InvalidateChunks();
};

} // namespace Rendering
