#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdio>

#include "core/ConfigManager.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "ecs/Buildings.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Health.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Renderer.h"

namespace Verify {

namespace {

using Input = Core::InputManager;
constexpr int kFrameW = 1024;
constexpr int kFrameH = 768;

// The cursor with the pointer at the middle of the frame, the view zoomed
// out to half size and centred on `thing`'s place.
Input::Intent IntentOver(ECS::Simulation& sim, entt::entity thing) {
    const auto& at = sim.GetRegistry().get<ECS::Components::Transform>(thing).position;
    Rendering::Renderer view;
    view.view_w = static_cast<float>(kFrameW);
    view.view_h = static_cast<float>(kFrameH);
    view.camera_zoom = view.target_camera_zoom = 0.5f;
    const SDL_FPoint p = Core::IsometricMath::WorldToScreen(at.x, at.y);
    view.camera_x = view.target_camera_x = p.x - view.view_w / 2.0f / view.camera_zoom;
    view.camera_y = view.target_camera_y = p.y - view.view_h / 2.0f / view.camera_zoom;
    auto& input = Input::Get();
    input.Update(view, 0.0f, kFrameW, kFrameH);
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = kFrameW / 2;
    motion.motion.y = kFrameH / 2;
    input.ProcessEvent(motion, sim, view);
    input.Hover(sim, view);
    return input.CursorIntent();
}

} // namespace

// A villager selected and the view zoomed out to half size: the pointer over
// a gold mine's foot, a House's foundation and a wounded House shows the
// hand, each found on the ground under the pointer as a click finds it.
bool CheckCursor() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const auto [sx, sy] = sim.player_starts[0];
    const int house = cfg.GetUnitIdByName("HOUS");
    const entt::entity villager = ECS::ScenarioWorld::Place(
        reg, grid, cfg.GetUnitIdByName("VMBAS"), 1, {Core::Fixed32(sx - 4), Core::Fixed32(sy + 4)}, {}, 0);
    const entt::entity mine = ECS::ScenarioWorld::Place(
        reg, grid, cfg.GetUnitIdByName("GOLDM"), 0, {Core::Fixed32(sx + 6), Core::Fixed32(sy + 6)}, {}, 0);
    const entt::entity site = ECS::Buildings::Place(reg, grid, house, 1, sx + 6, sy - 3, false);
    const entt::entity wounded = ECS::Buildings::Place(reg, grid, house, 1, sx - 3, sy + 7, true);
    int seen[3] = {-1, -1, -1};
    if (villager == entt::null || mine == entt::null || site == entt::null || wounded == entt::null) {
        verdict = "no villager, mine or Houses placed";
    } else {
        auto& hp = reg.get<ECS::Components::Health>(wounded);
        hp.current_hp = hp.max_hp / 2;
        sim.Update(16);
        auto all = reg.view<ECS::Components::Selectable>();
        for (auto e : all) all.get<ECS::Components::Selectable>(e).is_selected = e == villager;
        // A frame with nothing in sight, so nothing is found by its art.
        SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
        Rendering::Renderer away;
        away.camera_x = away.camera_y = -1.0e6f;
        if (r != nullptr) Rendering::EntityRenderer::DrawGroundLevel(sim, away, r, 1.0f);
        const entt::entity things[3] = {mine, site, wounded};
        static const char* const kVerdicts[3] = {
            "the pointer over a mine's foot, zoomed out, shows no hand",
            "the pointer over a foundation, zoomed out, shows no hand",
            "the pointer over a wounded building, zoomed out, shows no hand"};
        for (int i = 0; i < 3; ++i) {
            const Input::Intent intent = IntentOver(sim, things[i]);
            seen[i] = static_cast<int>(intent);
            if (verdict == nullptr && intent != Input::Intent::Work) verdict = kVerdicts[i];
        }
        if (r != nullptr) SDL_DestroyRenderer(r);
        if (screen != nullptr) SDL_FreeSurface(screen);
    }
    std::printf("cursor=%s mine=%d site=%d wounded=%d\n", verdict == nullptr ? "ok" : verdict, seen[0], seen[1],
                seen[2]);
    return verdict == nullptr;
}

} // namespace Verify
