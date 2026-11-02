#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/ConfigManager.h"
#include "hal/GraphicCache.h"
#include "hal/SlpFile.h"

namespace Verify {

namespace {

// The trebuchet's attack (2393 rows of frames shelved for a page) and the
// tallest graphic of all (3248).
constexpr uint32_t kTrebuchetAttack = 1237;
constexpr uint32_t kTallest = 4338;
// The interface's tallest graphic: 4807 rows shelved as a page is wide.
constexpr uint32_t kTallestInterface = 50612;
const char* const kGraphicsDrs = "../assets/original/Data/graphics.drs";
const char* const kInterfaceDrs = "../assets/original/Data/interfac.drs";
const char* const kPalette = "../assets/original/interfac_extracted/50500.bina";

// Whether frame `index` of `slp`, drawn from where its art was placed, is
// the frame as its SLP holds it in player 1's colours.
bool DrawsAsDecoded(SDL_Renderer* r, HAL::GraphicCache& cache, const HAL::DrsArchive& drs,
                    const std::vector<HAL::PaletteColor>& palette, uint32_t slp, size_t index) {
    const HAL::GraphicAtlas* atlas = cache.GetAtlas(slp);
    HAL::SlpFrame frame;
    if (atlas == nullptr || index >= atlas->frames.size() || !HAL::SlpDecodeFrame(drs, slp, index, frame)) {
        return false;
    }
    const HAL::GraphicFrame& f = atlas->frames[index];
    if (f.rect.w <= 0) return true;
    const HAL::ArtPlace place = cache.Art(slp, 1);
    SDL_Texture* page = place.PageOf(f.rect);
    if (page == nullptr) return false;
    const int w = f.rect.w, h = f.rect.h;
    SDL_Texture* target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (target == nullptr) return false;
    SDL_SetRenderTarget(r, target);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_SetTextureBlendMode(page, SDL_BLENDMODE_NONE);
    const SDL_Rect src = place.On(f.rect);
    SDL_RenderCopy(r, page, &src, nullptr);
    SDL_SetTextureBlendMode(page, SDL_BLENDMODE_BLEND);
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, nullptr);
    SDL_DestroyTexture(target);
    const auto& ramps = Core::ConfigManager::Get().PlayerRamps();
    const int slot = cache.ColourSlot(1);
    for (int i = 0; i < w * h; ++i) {
        const uint8_t* got = &pixels[static_cast<size_t>(i) * 4];
        if (!frame.alpha[static_cast<size_t>(i)]) {
            if (got[3] != 0) return false;
            continue;
        }
        const size_t colour = frame.player[static_cast<size_t>(i)]
            ? static_cast<size_t>(ramps[static_cast<size_t>(slot)]) + std::min<size_t>(frame.player[static_cast<size_t>(i)], 7)
            : frame.indexes[static_cast<size_t>(i)];
        const HAL::PaletteColor& c = palette[colour % palette.size()];
        if (got[0] != c.r || got[1] != c.g || got[2] != c.b || got[3] != 255) return false;
    }
    return true;
}

// The art graphics too tall for the textures of a renderer allowing `side`,
// on one, and with `interface` the interface's tallest: the verdict, or null.
const char* DrawnWithin(int side, bool interface, int& bands) {
    const char* verdict = nullptr;
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
    auto& cache = HAL::GraphicCache::Get();
    HAL::DrsArchive drs;
    std::vector<HAL::PaletteColor> palette;
    if (r == nullptr || !cache.Init(r, kGraphicsDrs, kPalette) || !HAL::DrsOpen(kGraphicsDrs, drs) ||
        !HAL::LoadJascPalette(kPalette, palette) || !cache.OpenInterface(kInterfaceDrs)) {
        verdict = "no renderer or no art";
    } else {
        cache.LimitTextureSide(side);
        cache.SetPlayerRamps(Core::ConfigManager::Get().PlayerRamps());
        for (const uint32_t slp : {kTrebuchetAttack, kTallest}) {
            const HAL::GraphicAtlas* atlas = cache.GetAtlas(slp);
            if (atlas == nullptr) {
                verdict = "no art for the tall graphics";
                break;
            }
            const HAL::ArtPlace place = cache.Art(slp, 1);
            if (place.bands != nullptr) bands += static_cast<int>(place.bands->size());
            for (size_t i = 0; i < atlas->frames.size() && verdict == nullptr; ++i) {
                if (!DrawsAsDecoded(r, cache, drs, palette, slp, i)) {
                    verdict = "a graphic taller than the renderer's textures does not draw as its SLP";
                }
            }
        }
        const HAL::GraphicAtlas* panel = interface ? cache.GetInterfaceAtlas(kTallestInterface) : nullptr;
        if (verdict == nullptr && interface && (panel == nullptr || panel->base == nullptr || panel->height > side ||
                                   panel->width > side)) {
            verdict = "an interface graphic is not within the renderer's textures";
        }
    }
    cache.Shutdown();
    // As the game's start left it: the scripts' archive open, no art.
    cache.OpenGameData("../assets/original/Data/gamedata.drs");
    if (r != nullptr) SDL_DestroyRenderer(r);
    if (screen != nullptr) SDL_FreeSurface(screen);
    return verdict;
}

} // namespace

// On a renderer whose textures are no bigger than 2048 on a side, as some
// phones', the trebuchet's attack and the tallest graphic, too tall for any
// page, are drawn frame for frame as their SLPs hold them, from bands of
// their shelves; on one allowing 4096 the interface's tallest graphic is
// shelved within a texture.
bool CheckSmallTextures() {
    int bands = 0;
    const char* verdict = DrawnWithin(2048, false, bands);
    if (verdict == nullptr) {
        int unused = 0;
        verdict = DrawnWithin(4096, true, unused);
    }
    std::printf("smalltextures=%s bands=%d\n", verdict == nullptr ? "ok" : verdict, bands);
    return verdict == nullptr;
}

} // namespace Verify
