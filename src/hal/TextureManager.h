#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <memory>

namespace HAL {

class TextureManager {
public:
    static TextureManager& Get() {
        static TextureManager instance;
        return instance;
    }

    bool Init(SDL_Renderer* renderer);
    void Shutdown();

    // Loads an image file and stores it with the given ID
    bool LoadTexture(const std::string& id, const std::string& filepath);
    
    // Creates a texture from an SDL_Surface (useful for generated textures)
    bool LoadFromSurface(const std::string& id, SDL_Surface* surface);

    SDL_Texture* GetTexture(const std::string& id);

private:
    TextureManager() = default;
    ~TextureManager() { Shutdown(); }
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    SDL_Renderer* sdl_renderer = nullptr;
    std::unordered_map<std::string, SDL_Texture*> textures;
};

} // namespace HAL
