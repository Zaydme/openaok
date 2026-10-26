#include "TextureManager.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <iostream>

namespace HAL {

bool TextureManager::Init(SDL_Renderer* renderer) {
    sdl_renderer = renderer;
    return true;
}

void TextureManager::Shutdown() {
    for (auto& pair : textures) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textures.clear();
}

bool TextureManager::LoadTexture(const std::string& id, const std::string& filepath) {
    if (!sdl_renderer) return false;
    if (textures.find(id) != textures.end()) return true; // Already loaded

    int width, height, channels;
    unsigned char* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "[HAL] Unable to load image " << filepath << "! stb_image Error: " << stbi_failure_reason() << std::endl;
        return false;
    }

    Uint32 rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000; gmask = 0x00ff0000; bmask = 0x0000ff00; amask = 0x000000ff;
#else
    rmask = 0x000000ff; gmask = 0x0000ff00; bmask = 0x00ff0000; amask = 0xff000000;
#endif

    int depth = 32;
    int pitch = 4 * width;
    SDL_Surface* loadedSurface = SDL_CreateRGBSurfaceFrom((void*)pixels, width, height, depth, pitch, rmask, gmask, bmask, amask);

    if (!loadedSurface) {
        std::cerr << "[HAL] Unable to create surface from " << filepath << "! SDL Error: " << SDL_GetError() << std::endl;
        stbi_image_free(pixels);
        return false;
    }

    SDL_Texture* newTexture = SDL_CreateTextureFromSurface(sdl_renderer, loadedSurface);
    SDL_FreeSurface(loadedSurface);
    stbi_image_free(pixels);

    if (!newTexture) {
        std::cerr << "[HAL] Unable to create texture from " << filepath << "! SDL Error: " << SDL_GetError() << std::endl;
        return false;
    }

    textures[id] = newTexture;
    return true;
}

bool TextureManager::LoadFromSurface(const std::string& id, SDL_Surface* surface) {
    if (!sdl_renderer || !surface) return false;
    if (textures.find(id) != textures.end()) {
        SDL_DestroyTexture(textures[id]); // Overwrite
    }

    SDL_Texture* newTexture = SDL_CreateTextureFromSurface(sdl_renderer, surface);
    if (!newTexture) return false;

    textures[id] = newTexture;
    return true;
}

SDL_Texture* TextureManager::GetTexture(const std::string& id) {
    auto it = textures.find(id);
    if (it != textures.end()) {
        return it->second;
    }
    
    // Fallback: If "something_XX" isn't found, try "something_0"
    size_t last_underscore = id.find_last_of('_');
    if (last_underscore != std::string::npos) {
        std::string base = id.substr(0, last_underscore + 1);
        std::string fallback_id = base + "0";
        auto fallback_it = textures.find(fallback_id);
        if (fallback_it != textures.end()) {
            return fallback_it->second;
        }
    }
    
    // Auto-load if the ID looks like a file path
    if (id.find(".png") != std::string::npos || id.find(".jpg") != std::string::npos) {
        if (LoadTexture(id, id)) {
            return textures[id];
        } else {
            // Cache the failure so a missing file is not retried every frame
            textures[id] = nullptr;
        }
    }
    
    return nullptr;
}

} // namespace HAL
