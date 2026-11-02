#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "core/ConfigManager.h"
#include "hal/GraphicCache.h"
#include "hal/SlpFile.h"

namespace Verify {

namespace {

constexpr int kVillager = 83;
constexpr int kGraphics = 300;    // graphics' art placed, past the villager's
constexpr int kMostPages = 8;     // textures they may take between them
const char* const kGraphicsDrs = "../assets/original/Data/graphics.drs";
const char* const kPalette = "../assets/original/interfac_extracted/50500.bina";

// Whether one frame drawn from its page, with a pixel more all round, is the
// frame as its SLP holds it in `player`'s colours, clear about it.
bool DrawsAsDecoded(SDL_Renderer* r, const HAL::ArtPlace& place, const HAL::GraphicFrame& f,
                    const HAL::SlpFrame& frame, const std::vector<HAL::PaletteColor>& palette, int player) {
    const int w = f.rect.w + 2, h = f.rect.h + 2;
    SDL_Texture* target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (target == nullptr) return false;
    SDL_SetRenderTarget(r, target);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_Texture* page = place.PageOf(f.rect);
    SDL_SetTextureBlendMode(page, SDL_BLENDMODE_NONE);
    const SDL_Rect on = place.On(f.rect);
    const SDL_Rect src{on.x - 1, on.y - 1, w, h};
    const SDL_Rect dst{0, 0, w, h};
    SDL_RenderCopy(r, page, &src, &dst);
    SDL_SetTextureBlendMode(page, SDL_BLENDMODE_BLEND);
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    SDL_SetRenderTarget(r, nullptr);
    SDL_DestroyTexture(target);

    const auto& ramps = Core::ConfigManager::Get().PlayerRamps();
    const int slot = HAL::GraphicCache::Get().ColourSlot(player);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* got = &pixels[(static_cast<size_t>(y) * w + x) * 4];
            const int fx = x - 1, fy = y - 1;
            const bool inside = fx >= 0 && fy >= 0 && fx < static_cast<int>(frame.width) &&
                                fy < static_cast<int>(frame.height);
            const size_t p = inside ? static_cast<size_t>(fy) * frame.width + fx : 0;
            if (!inside || !frame.alpha[p]) {
                if (got[3] != 0) return false;
                continue;
            }
            const size_t index = frame.player[p]
                                     ? static_cast<size_t>(ramps[static_cast<size_t>(slot)]) +
                                           std::min<size_t>(frame.player[p], 7)
                                     : frame.indexes[p];
            const HAL::PaletteColor& c = palette[index % palette.size()];
            if (got[0] != c.r || got[1] != c.g || got[2] != c.b || got[3] != 255) return false;
        }
    }
    return true;
}

} // namespace

namespace {

// Places the villager and kGraphics more graphics on `r`'s pages while the
// frame's target is drawn at half size, as the view zoomed out draws it: the
// verdict, or null, with how many pages the art took and whether one grew.
const char* PagesOn(SDL_Renderer* r, int& pages, bool& grew) {
    const char* verdict = nullptr;
    auto& cache = HAL::GraphicCache::Get();
    HAL::DrsArchive drs;
    std::vector<HAL::PaletteColor> palette;
    const auto art = Core::ConfigManager::Get().GetUnitGraphicById(kVillager, "idle_graphic0");
    SDL_Texture* frame = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (frame == nullptr || !cache.Init(r, kGraphicsDrs, kPalette) || !HAL::DrsOpen(kGraphicsDrs, drs) ||
        !HAL::LoadJascPalette(kPalette, palette) || !art.Valid()) {
        verdict = "no renderer or no art";
    } else {
        cache.SetPlayerRamps(Core::ConfigManager::Get().PlayerRamps());
        const auto slp = static_cast<uint32_t>(art.slp_id);
        const HAL::GraphicAtlas* atlas = cache.GetAtlas(slp);
        HAL::SlpFrame decoded;
        if (atlas == nullptr || atlas->frames.empty() || !HAL::SlpDecodeFrame(drs, slp, 0, decoded)) {
            verdict = "no villager art";
        } else {
            auto drawn = [&] {
                for (int player : {1, 2}) {
                    if (!DrawsAsDecoded(r, cache.Art(slp, player), atlas->frames[0], decoded, palette, player)) {
                        return false;
                    }
                }
                return true;
            };
            const HAL::ArtPlace first = cache.Art(slp, 1);
            if (!drawn()) verdict = "a villager drawn from its page is not its SLP frame in its player's colours";
            std::vector<uint32_t> ids;
            for (const auto& [id, entry] : drs.entries) ids.push_back(id);
            std::sort(ids.begin(), ids.end());
            SDL_SetRenderTarget(r, frame);
            SDL_RenderSetScale(r, 0.5f, 0.5f);
            std::vector<uint32_t> placed;
            for (uint32_t id : ids) {
                if (static_cast<int>(placed.size()) >= kGraphics) break;
                if (id == slp || cache.GetAtlas(id) == nullptr) continue;
                if (cache.Art(id, 1).page != nullptr) placed.push_back(id);
            }
            float scale_x = 0.0f, scale_y = 0.0f;
            SDL_RenderGetScale(r, &scale_x, &scale_y);
            const bool kept = SDL_GetRenderTarget(r) == frame && scale_x == 0.5f && scale_y == 0.5f;
            SDL_RenderSetScale(r, 1.0f, 1.0f);
            SDL_SetRenderTarget(r, nullptr);
            // Where they all are once every page has grown as it had to.
            std::set<SDL_Texture*> used;
            for (uint32_t id : placed) used.insert(cache.Art(id, 1).page);
            pages = static_cast<int>(used.size());
            grew = cache.Art(slp, 1).page != first.page;
            if (verdict == nullptr && pages > kMostPages) verdict = "graphics' art did not share pages";
            if (verdict == nullptr && !grew) verdict = "a page never grew as art was placed on it";
            if (verdict == nullptr && !kept) verdict = "placing art changed the frame's target or zoom";
            if (verdict == nullptr && !drawn()) verdict = "a villager drawn from its page after it grew is not its SLP frame";
            // Every graphic placed, those whose placing grew a page among
            // them, drawn from its page as its SLP holds it.
            for (uint32_t id : placed) {
                if (verdict != nullptr) break;
                const HAL::GraphicAtlas* a = cache.GetAtlas(id);
                HAL::SlpFrame f;
                if (a == nullptr || a->frames.empty() || a->frames[0].rect.w <= 0 || !HAL::SlpDecodeFrame(drs, id, 0, f)) {
                    continue;
                }
                if (!DrawsAsDecoded(r, cache.Art(id, 1), a->frames[0], f, palette, 1)) {
                    verdict = "a graphic placed as its page grew is not its SLP frame";
                }
            }
        }
    }
    cache.Shutdown();
    // As the game's start left it: the scripts' archive open, no art.
    cache.OpenGameData("../assets/original/Data/gamedata.drs");
    if (frame != nullptr) SDL_DestroyTexture(frame);
    return verdict;
}

// The index of the render driver named `name`, or -1.
int DriverIndex(const char* name) {
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
        SDL_RendererInfo info{};
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && std::strcmp(info.name, name) == 0) return i;
    }
    return -1;
}

} // namespace

// The art of many graphics shares a page or two, and each page holds every
// graphic's own pixels: a villager drawn from its page is, pixel for pixel,
// its SLP frame in the colours of the player it is drawn for -- its colour
// pixels from the palette, its player-coloured ones from the ramp of the
// colour worn -- clear all round it, and still so after its page has grown to
// take the art placed after it; and so is every graphic placed after it,
// those placed as a page grew among them. Placing them leaves the frame being
// drawn at its zoom. With the software renderer, and with Metal (iOS's) where
// there is one.
bool CheckPages() {
    int pages = 0;
    bool grew = false;
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
    const char* verdict = r != nullptr ? PagesOn(r, pages, grew) : "no renderer";
    if (r != nullptr) SDL_DestroyRenderer(r);
    if (screen != nullptr) SDL_FreeSurface(screen);

    const char* metal = "none";
    const int driver = DriverIndex("metal");
    if (verdict == nullptr && driver >= 0 && SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
        SDL_Window* window = SDL_CreateWindow("pages", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
        SDL_Renderer* gpu = window != nullptr
            ? SDL_CreateRenderer(window, driver, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE)
            : nullptr;
        if (gpu != nullptr) {
            int gpu_pages = 0;
            bool gpu_grew = false;
            verdict = PagesOn(gpu, gpu_pages, gpu_grew);
            metal = "ok";
            SDL_DestroyRenderer(gpu);
        }
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    std::printf("pages=%s pages=%d grew=%d metal=%s\n", verdict == nullptr ? "ok" : verdict, pages, grew ? 1 : 0,
                verdict == nullptr ? metal : "-");
    return verdict == nullptr;
}

} // namespace Verify
