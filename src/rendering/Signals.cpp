#include "rendering/Signals.h"

#include <cmath>

#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "hal/GraphicCache.h"
#include "rendering/UIRenderer.h"

namespace Rendering::Signals {

namespace {

// A flare on the minimap: two one-pixel lines corner to corner, 4 pixels each
// way from the flare, in pale yellow (kLit) and white (kUnlit) by turns --
// whoever sent it. The two flip every 333 ms of the wall clock.
constexpr int kHalf = 4;
constexpr uint32_t kBlinkMs = 333;
constexpr size_t kLit = 0x45;
constexpr size_t kUnlit = 0xff;

} // namespace

void DrawOnMinimap(ECS::Simulation& sim, SDL_Renderer* sdl_r, const SDL_FRect& well,
                   Core::PlayerId viewer) {
    // The minimap's blink, kept whether or not a flare is up.
    static uint32_t flipped = 0;
    static bool lit = false;
    const uint32_t now = SDL_GetTicks();
    if (now - flipped >= kBlinkMs) {
        flipped = now;
        lit = !lit;
    }

    const auto& registry = sim.GetRegistry();
    const ECS::Flares::Log* flares = ECS::Flares::Read(registry);
    const ECS::Events::Log* events = ECS::Events::Read(registry);
    if (flares == nullptr || events == nullptr || flares->flares.empty()) return;
    const auto& grid = sim.GetGrid();
    const HAL::PaletteColor colour = HAL::GraphicCache::Get().GetPaletteColor(lit ? kLit : kUnlit);
    SDL_SetRenderDrawColor(sdl_r, colour.r, colour.g, colour.b, 255);
    for (const ECS::Flares::Flare& flare : flares->flares) {
        if (!flare.Seen(viewer) || !ECS::Flares::Showing(*flares, flare, events->tick)) continue;
        const SDL_FPoint p = UIRenderer::MinimapProject(well, flare.at.x.ToFloat(),
                                                        flare.at.y.ToFloat(), grid.GetWidth(),
                                                        grid.GetHeight());
        const int x = static_cast<int>(std::floor(p.x));
        const int y = static_cast<int>(std::floor(p.y));
        SDL_RenderDrawLine(sdl_r, x - kHalf, y - kHalf, x + kHalf, y + kHalf);
        SDL_RenderDrawLine(sdl_r, x - kHalf, y + kHalf, x + kHalf, y - kHalf);
    }
}

} // namespace Rendering::Signals
