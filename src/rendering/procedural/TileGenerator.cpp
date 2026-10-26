#include "TileGenerator.h"
#include "hal/TextureManager.h"
#include "core/IsometricMath.h"
#include <cmath>
#include <random>
#include <algorithm>

namespace Rendering::Procedural {

void TileGenerator::ApplyNoise([[maybe_unused]] SDL_Surface* surface, int intensity, [[maybe_unused]] int scale) {
    std::mt19937 rng(42); // Deterministic noise for stable visuals
    std::uniform_int_distribution<int> dist(-intensity, intensity);
    // No noise is applied: the tiles stay flat-shaded.
    return;
}

void TileGenerator::MaskToIsometric(SDL_Surface* surface) {
    SDL_LockSurface(surface);
    uint32_t* pixels = static_cast<uint32_t*>(surface->pixels);
    int pitch = surface->pitch / 4;
    
    int w = surface->w;
    int h = surface->h;
    float hw = w / 2.0f;
    float hh = h / 2.0f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Isometric diamond distance equation: |x - hw|/hw + |y - hh|/hh <= 1
            float dx = std::abs(x - hw) / hw;
            float dy = std::abs(y - hh) / hh;
            
            if (dx + dy > 1.0f) {
                pixels[y * pitch + x] = SDL_MapRGBA(surface->format, 0, 0, 0, 0); // Transparent
            }
        }
    }
    SDL_UnlockSurface(surface);
}

void TileGenerator::GenerateGrassVariant(SDL_Renderer*, const std::string& id, uint8_t r, uint8_t g, uint8_t b) {
    int w = Core::IsometricMath::TILE_WIDTH;
    int h = Core::IsometricMath::TILE_HEIGHT;
    
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, r, g, b, 255));
    
    ApplyNoise(surface, 15, 2);
    ApplyNoise(surface, 25, 8);
    MaskToIsometric(surface);
    
    HAL::TextureManager::Get().LoadFromSurface(id, surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateDirtVariant(SDL_Renderer*, const std::string& id, uint8_t r, uint8_t g, uint8_t b) {
    int w = Core::IsometricMath::TILE_WIDTH;
    int h = Core::IsometricMath::TILE_HEIGHT;
    
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, r, g, b, 255));
    
    ApplyNoise(surface, 20, 1);
    ApplyNoise(surface, 35, 6);
    MaskToIsometric(surface);
    
    HAL::TextureManager::Get().LoadFromSurface(id, surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateSand(SDL_Renderer*, const std::string& id) {
    int w = Core::IsometricMath::TILE_WIDTH;
    int h = Core::IsometricMath::TILE_HEIGHT;
    
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 194, 178, 128, 255));
    
    ApplyNoise(surface, 10, 1);
    MaskToIsometric(surface);
    
    HAL::TextureManager::Get().LoadFromSurface(id, surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateWater(SDL_Renderer*, const std::string& id, bool deep) {
    int w = Core::IsometricMath::TILE_WIDTH;
    int h = Core::IsometricMath::TILE_HEIGHT;
    
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (deep) {
        SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 25, 65, 130, 200)); // Semi-transparent
    } else {
        SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 60, 130, 180, 200));
    }
    
    ApplyNoise(surface, 10, 4);
    MaskToIsometric(surface);
    
    HAL::TextureManager::Get().LoadFromSurface(id, surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateTree(SDL_Renderer*, const std::string& id) {
    // A placeholder tree sprite (not an isometric tile, but an entity sprite)
    int w = 64;
    int h = 96;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));
    
    // Trunk
    SDL_Rect trunk = {w/2 - 4, h - 20, 8, 20};
    SDL_FillRect(surface, &trunk, SDL_MapRGBA(surface->format, 90, 60, 30, 255));
    
    // Canopy (simple layered rects to simulate a pine tree)
    for (int i = 0; i < 4; ++i) {
        int width = 40 - i * 8;
        int height = 20;
        int y = h - 35 - i * 15;
        SDL_Rect branch = {w/2 - width/2, y, width, height};
        SDL_FillRect(surface, &branch, SDL_MapRGBA(surface->format, 25, 80 + i * 10, 30, 255));
    }
    
    HAL::TextureManager::Get().LoadFromSurface(id, surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateSelectionCircle(SDL_Renderer*) {
    int w = 64;
    int h = 32; // Isometric ellipse
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));
    
    SDL_LockSurface(surface);
    uint32_t* pixels = static_cast<uint32_t*>(surface->pixels);
    int pitch = surface->pitch / 4;
    
    float hw = w / 2.0f;
    float hh = h / 2.0f;
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = (x - hw) / hw;
            float dy = (y - hh) / hh;
            float dist = dx * dx + dy * dy;
            
            // Draw ring
            if (dist > 0.8f && dist < 1.0f) {
                pixels[y * pitch + x] = SDL_MapRGBA(surface->format, 50, 255, 50, 200);
            }
        }
    }
    SDL_UnlockSurface(surface);
    HAL::TextureManager::Get().LoadFromSurface("ui_selection", surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateUnitSpriteSheet(SDL_Renderer*, const std::string& id) {
    int frames = 4;
    int dirs = 8;
    int w = 32;
    int h = 32;
    
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w * frames, h * dirs, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));
    
    for (int dir = 0; dir < dirs; ++dir) {
        for (int frame = 0; frame < frames; ++frame) {
            int cx = frame * w + w / 2;
            int cy = dir * h + h / 2;
            
            // Head
            SDL_Rect head = {cx - 4, cy - 12, 8, 8};
            SDL_FillRect(surface, &head, SDL_MapRGBA(surface->format, 220, 180, 140, 255));
            
            // Body
            SDL_Rect body = {cx - 5, cy - 4, 10, 12};
            SDL_FillRect(surface, &body, SDL_MapRGBA(surface->format, 30, 80, 200, 255));
            
            // Legs (animation)
            int lx = cx - 3;
            int rx = cx + 1;
            int ly = cy + 8;
            int ry = cy + 8;
            
            // Simple walk cycle bounce
            if (frame == 1) { ly -= 2; rx += 2; }
            if (frame == 3) { ry -= 2; lx -= 2; }
            
            SDL_Rect l_leg = {lx, ly, 3, 6};
            SDL_Rect r_leg = {rx, ry, 3, 6};
            SDL_FillRect(surface, &l_leg, SDL_MapRGBA(surface->format, 100, 100, 100, 255));
            SDL_FillRect(surface, &r_leg, SDL_MapRGBA(surface->format, 100, 100, 100, 255));
        }
    }
    
    HAL::TextureManager::Get().LoadFromSurface(id, surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateFogTile(SDL_Renderer*) {
    int w = Core::IsometricMath::TILE_WIDTH;
    int h = Core::IsometricMath::TILE_HEIGHT;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 255, 255, 255, 255));
    MaskToIsometric(surface);
    HAL::TextureManager::Get().LoadFromSurface("ui_fog", surface);
    SDL_FreeSurface(surface);
}

void TileGenerator::GenerateAllTiles(SDL_Renderer* renderer) {
    GenerateGrassVariant(renderer, "tile_grass1", 72, 140, 48);
    GenerateGrassVariant(renderer, "tile_grass2", 82, 152, 56);
    GenerateGrassVariant(renderer, "tile_grass3", 65, 128, 42);
    
    GenerateDirtVariant(renderer, "tile_dirt1", 140, 110, 70);
    GenerateDirtVariant(renderer, "tile_dirt2", 128, 100, 62);
    
    GenerateSand(renderer, "tile_sand");
    
    GenerateWater(renderer, "tile_water_shallow", false);
    GenerateWater(renderer, "tile_water_deep", true);
    
    GenerateFogTile(renderer);
}

} // namespace Rendering::Procedural
