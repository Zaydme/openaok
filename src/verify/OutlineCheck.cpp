#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <vector>

#include "core/ConfigManager.h"
#include "core/IsometricMath.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Transform.h"
#include "hal/GraphicCache.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Renderer.h"

namespace Verify {

namespace {

const char* const kGraphicsDrs = "../assets/original/Data/graphics.drs";
const char* const kPalette = "../assets/original/interfac_extracted/50500.bina";
constexpr int kFrameW = 1024;
constexpr int kFrameH = 768;

// One frame of the world's sprites drawn about `at`: how many silhouettes it
// drew.
int OutlinesAbout(ECS::Simulation& sim, SDL_Renderer* r, const ECS::Components::Vector2D& at) {
    Rendering::Renderer view;
    view.view_w = static_cast<float>(kFrameW);
    view.view_h = static_cast<float>(kFrameH);
    const SDL_FPoint p = Core::IsometricMath::WorldToScreen(at.x, at.y);
    view.camera_x = view.target_camera_x = p.x - view.view_w / 2.0f;
    view.camera_y = view.target_camera_y = p.y - view.view_h / 2.0f;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    Rendering::EntityRenderer::DrawGroundLevel(sim, view, r, 1.0f);
    Rendering::EntityRenderer::DrawStanding(view, r);
    return Rendering::EntityRenderer::OutlinesDrawn();
}

} // namespace

// A trebuchet set up in the open shows no silhouette, tall as it is; one with
// a Castle standing in front of it shows its silhouette through the Castle.
bool CheckOutline() {
    const char* verdict = nullptr;
    int open = -1, hidden = -1;
    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, kFrameW, kFrameH, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
    auto& cache = HAL::GraphicCache::Get();
    if (r == nullptr || !cache.Init(r, kGraphicsDrs, kPalette)) {
        verdict = "no renderer or no art";
    } else {
        cache.SetPlayerRamps(Core::ConfigManager::Get().PlayerRamps());
        ECS::Simulation sim;
        sim.Init();
        auto& reg = sim.GetRegistry();
        auto& cfg = Core::ConfigManager::Get();
        sim.Update(16);
        const auto [sx, sy] = sim.player_starts[0];
        const ECS::Components::Vector2D spot{Core::Fixed32(sx + 8), Core::Fixed32(sy + 8)};
        // Nothing else about with a silhouette of its own: the start's units,
        // its mines and bushes.
        std::vector<entt::entity> others;
        auto placed = reg.view<ECS::Components::Transform>();
        for (auto e : placed) {
            if (reg.all_of<ECS::Components::Building>(e)) continue;
            const auto& at = placed.get<ECS::Components::Transform>(e).position;
            const int dx = at.x.ToInt() - sx, dy = at.y.ToInt() - sy;
            if (dx * dx + dy * dy < 30 * 30) others.push_back(e);
        }
        for (entt::entity e : others) reg.destroy(e);
        const entt::entity trebuchet =
            ECS::ScenarioWorld::Place(reg, sim.GetGrid(), cfg.GetUnitIdByName("TREBU"), 1, spot, {}, 0);
        if (trebuchet == entt::null) {
            verdict = "no trebuchet placed";
        } else {
            sim.Update(16);
            open = OutlinesAbout(sim, r, spot);
            const entt::entity castle = ECS::ScenarioWorld::Place(
                reg, sim.GetGrid(), cfg.GetUnitIdByName("CSTL"), 1,
                {Core::Fixed32(sx + 11), Core::Fixed32(sy + 11)}, {}, 0);
            if (castle == entt::null) {
                verdict = "no Castle placed";
            } else {
                sim.Update(16);
                hidden = OutlinesAbout(sim, r, spot);
                if (open != 0) {
                    verdict = "a trebuchet in the open shows its silhouette";
                } else if (hidden != 1) {
                    verdict = "a trebuchet behind a Castle does not show its silhouette";
                }
            }
        }
    }
    cache.Shutdown();
    // As the game's start left it: the scripts' archive open, no art.
    cache.OpenGameData("../assets/original/Data/gamedata.drs");
    if (r != nullptr) SDL_DestroyRenderer(r);
    if (screen != nullptr) SDL_FreeSurface(screen);
    std::printf("outline=%s open=%d hidden=%d\n", verdict == nullptr ? "ok" : verdict, open, hidden);
    return verdict == nullptr;
}

} // namespace Verify
