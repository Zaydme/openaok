#pragma once
#include <SDL2/SDL.h>
#include <string>

namespace Rendering::Procedural {

class TileGenerator {
public:
    static void GenerateAllTiles(SDL_Renderer* renderer);
    
private:
    static void GenerateGrassVariant(SDL_Renderer* renderer, const std::string& id, uint8_t r, uint8_t g, uint8_t b);
    static void GenerateDirtVariant(SDL_Renderer* renderer, const std::string& id, uint8_t r, uint8_t g, uint8_t b);
    static void GenerateSand(SDL_Renderer* renderer, const std::string& id);
    static void GenerateWater(SDL_Renderer* renderer, const std::string& id, bool deep);
    static void GenerateTree(SDL_Renderer* renderer, const std::string& id);
    static void GenerateSelectionCircle(SDL_Renderer* renderer);
    static void GenerateUnitSpriteSheet(SDL_Renderer* renderer, const std::string& id);
    static void GenerateFogTile(SDL_Renderer* renderer);

    // Helpers
    static void ApplyNoise(SDL_Surface* surface, int intensity, int scale);
    static void ApplyGradient(SDL_Surface* surface, uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);
    static void MaskToIsometric(SDL_Surface* surface);
};

} // namespace Rendering::Procedural
