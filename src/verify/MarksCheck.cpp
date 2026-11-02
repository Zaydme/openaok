#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "rendering/EntityRenderer.h"

namespace Verify {

namespace {

constexpr int kFrame = 160;   // the frame's side, in its pixels

struct Seen {
    int white = 0, green = 0, red = 0;
};

// A wounded unit's or building's marks drawn at `zoom` in the middle of the
// frame, and how many of the frame's pixels each colour took.
Seen Drawn(SDL_Renderer* r, SDL_Surface* screen, float zoom, bool round) {
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderSetScale(r, zoom, zoom);
    const int at = static_cast<int>(kFrame / 2 / zoom);
    const float radius = round ? 0.2f : 1.5f;
    Rendering::EntityRenderer::DrawMarks(r, at, at, radius, radius, round, 20, 40);
    SDL_RenderPresent(r);
    Seen seen;
    const auto* px = static_cast<const uint32_t*>(screen->pixels);
    for (int i = 0; i < kFrame * kFrame; ++i) {
        uint8_t cr = 0, cg = 0, cb = 0;
        SDL_GetRGB(px[i], screen->format, &cr, &cg, &cb);
        if (cr == 255 && cg == 255 && cb == 255) ++seen.white;
        if (cr == 0 && cg == 255 && cb == 0) ++seen.green;
        if (cr == 255 && cg == 0 && cb == 0) ++seen.red;
    }
    return seen;
}

} // namespace

// A selected unit's outline and hit point bar, and a building's, drawn with
// the view zoomed as far out as it goes: the outline still shows all round,
// and the bar still shows its green and its red, two rows of the frame high.
bool CheckMarks() {
    const char* verdict = nullptr;
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, kFrame, kFrame, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
    Seen near, far, building;
    if (r == nullptr) {
        verdict = "no renderer";
    } else {
        near = Drawn(r, screen, 1.0f, true);
        far = Drawn(r, screen, 0.4f, true);
        building = Drawn(r, screen, 0.4f, false);
        // The bar is 24 by 2 at the world's size: 12 of its pixels in each
        // colour, its row count kept, and fewer columns zoomed out.
        if (near.green != 24 || near.red != 24) {
            verdict = "the hit point bar is not 24 by 2 at the world's size";
        } else if (far.green < 2 * 4 || far.red < 2 * 4) {
            verdict = "the hit point bar thins away zoomed out";
        } else if (far.white < near.white / 4 || building.white < near.white) {
            verdict = "the selection outline thins away zoomed out";
        }
    }
    if (r != nullptr) SDL_DestroyRenderer(r);
    if (screen != nullptr) SDL_FreeSurface(screen);
    std::printf("marks=%s near=%d far=%d building=%d bar=%d/%d\n", verdict == nullptr ? "ok" : verdict,
                near.white, far.white, building.white, far.green, far.red);
    return verdict == nullptr;
}

} // namespace Verify
