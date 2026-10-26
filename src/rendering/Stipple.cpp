#include "rendering/Stipple.h"

#include <vector>

#include "rendering/Renderer.h"

namespace Rendering::Stipple {

namespace {

struct State {
    SDL_Texture* layer = nullptr;
    SDL_Texture* patterns[2] = {nullptr, nullptr};
    int w = 0;
    int h = 0;
    SDL_Texture* frame = nullptr;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
};
State g;

// Whether the pattern takes the pixel at (x, y) of the frame. The rows line
// up with the fog's checkerboard, which blacks out x + y even.
bool Takes(Pattern pattern, int x, int y) {
    switch (pattern) {
    case Pattern::Quarter: return (y & 1) != 0 ? (x & 3) == 3 : (x & 3) == 1;
    case Pattern::Half: return ((x + y) & 1) != 0;
    }
    return false;
}

// The layer and the patterns, made once, again only if the frame's size
// changes.
bool Ensure(SDL_Renderer* sdl_r, int w, int h) {
    if (g.layer != nullptr && g.w == w && g.h == h) return true;
    if (g.layer != nullptr) SDL_DestroyTexture(g.layer);
    for (SDL_Texture*& p : g.patterns) {
        if (p != nullptr) SDL_DestroyTexture(p);
        p = nullptr;
    }
    g.w = w;
    g.h = h;
    g.layer = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (g.layer == nullptr) return false;
    SDL_SetTextureBlendMode(g.layer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g.layer, SDL_ScaleModeNearest);

    // Multiplies the layer's alpha by the pattern's, colour untouched.
    static const SDL_BlendMode multiply_alpha = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int p = 0; p < 2; ++p) {
        const Pattern pattern = static_cast<Pattern>(p);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                    Takes(pattern, x, y) ? 0xFFFFFFFFu : 0u;
            }
        }
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
            pixels.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
        g.patterns[p] = surface != nullptr ? SDL_CreateTextureFromSurface(sdl_r, surface) : nullptr;
        if (surface != nullptr) SDL_FreeSurface(surface);
        if (g.patterns[p] == nullptr) return false;
        SDL_SetTextureBlendMode(g.patterns[p], multiply_alpha);
    }
    return true;
}

} // namespace

bool Begin(Renderer& renderer, SDL_Renderer* sdl_r) {
    if (!Ensure(sdl_r, static_cast<int>(renderer.view_w), static_cast<int>(renderer.view_h))) {
        return false;
    }
    // Drawing into the layer goes at whatever scale the caller draws at.
    SDL_RenderGetScale(sdl_r, &g.scale_x, &g.scale_y);
    g.frame = SDL_GetRenderTarget(sdl_r);
    SDL_SetRenderTarget(sdl_r, g.layer);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl_r, 0, 0, 0, 0);
    SDL_RenderClear(sdl_r);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    SDL_RenderSetScale(sdl_r, g.scale_x, g.scale_y);
    return true;
}

void End(SDL_Renderer* sdl_r, Pattern pattern, SDL_Color colour) {
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    // The silhouettes' colour becomes the one given, their coverage kept.
    static const SDL_BlendMode tint = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    SDL_SetRenderDrawBlendMode(sdl_r, tint);
    SDL_SetRenderDrawColor(sdl_r, colour.r, colour.g, colour.b, 255);
    SDL_RenderFillRect(sdl_r, nullptr);
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(sdl_r, g.patterns[static_cast<int>(pattern)], nullptr, nullptr);
    SDL_SetRenderTarget(sdl_r, g.frame);
    SDL_RenderSetScale(sdl_r, 1.0f, 1.0f);
    SDL_RenderCopy(sdl_r, g.layer, nullptr, nullptr);
    SDL_RenderSetScale(sdl_r, g.scale_x, g.scale_y);
    g.frame = nullptr;
}

} // namespace Rendering::Stipple
