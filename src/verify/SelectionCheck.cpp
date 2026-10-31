#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <SDL.h>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "core/Profile.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Selectable.h"
#include "rendering/Renderer.h"

namespace Verify {

namespace {

constexpr int kWinW = 1024;
constexpr int kWinH = 768;
constexpr int kUnits = 60;   // more than the original's forty
constexpr int kSide = 8;     // the square they stand in, eight a side, half a tile apart

void Mouse(Core::InputManager& input, ECS::Simulation& sim, Rendering::Renderer& r, Uint32 type, int x, int y) {
    SDL_Event e{};
    e.type = type;
    if (type == SDL_MOUSEMOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = x;
        e.button.y = y;
    }
    input.ProcessEvent(e, sim, r);
}

// A box drawn round the whole square: how many it selects, and of them how
// many of `units`.
int BoxSelect(ECS::Simulation& sim, Rendering::Renderer& r, const std::vector<entt::entity>& units, int& of_units) {
    auto& input = Core::InputManager::Get();
    input.Update(r, 0.0f, kWinW, kWinH);
    Mouse(input, sim, r, SDL_MOUSEBUTTONDOWN, 100, 100);
    Mouse(input, sim, r, SDL_MOUSEMOTION, 900, 420);
    Mouse(input, sim, r, SDL_MOUSEBUTTONUP, 900, 420);
    int selected = 0;
    auto view = sim.GetRegistry().view<ECS::Components::Selectable>();
    for (auto e : view) selected += view.get<ECS::Components::Selectable>(e).is_selected ? 1 : 0;
    of_units = 0;
    for (auto e : units) of_units += view.get<ECS::Components::Selectable>(e).is_selected ? 1 : 0;
    return selected;
}

} // namespace

// The selection limit, which the original holds at forty: sixty of the
// player's militia in a square, swept up by a box drawn round them, are all
// selected with the limit off, as it is by default, and forty things in all
// with it on.
bool CheckSelection() {
    const char* verdict = "ok";
    auto& profile = Core::Profile::Current();
    const Core::Profile::Options kept = profile;
    profile.top_bar_size = profile.panel_size = profile.text_size = 100;

    Core::GameSetup setup = Core::DefaultSetup();
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    const int militia = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
    const auto& start = sim.player_starts[0];
    const int x0 = start.first + 3, y0 = start.second + 3;
    const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
    std::vector<entt::entity> units;
    for (int i = 0; i < kUnits; ++i) {
        units.push_back(ECS::Units::Spawn(reg, militia, 1,
                          {Core::Fixed32(x0) + half * Core::Fixed32(i % kSide),
                           Core::Fixed32(y0) + half * Core::Fixed32(i / kSide)}));
    }
    // The square's middle at the middle of the world's part of the window.
    Rendering::Renderer r;
    const Core::Fixed32 mid = half * Core::Fixed32(kSide - 1) / Core::Fixed32(2);
    const SDL_FPoint at = Core::IsometricMath::WorldToScreen(Core::Fixed32(x0) + mid, Core::Fixed32(y0) + mid);
    r.camera_x = r.target_camera_x = at.x - 500.0f;
    r.camera_y = r.target_camera_y = at.y - 260.0f;

    int unlimited = 0, of_units = 0;
    profile.no_selection_limit = true;
    BoxSelect(sim, r, units, unlimited);
    profile.no_selection_limit = false;
    const int limited = BoxSelect(sim, r, units, of_units);
    if (Core::Profile::Options{}.no_selection_limit != true) {
        verdict = "the selection limit is on by default";
    } else if (unlimited != kUnits) {
        verdict = "with no limit, the box did not select every unit in it";
    } else if (limited != Core::InputManager::kOriginalSelectionLimit) {
        verdict = "with the limit on, the box did not select forty";
    }
    profile = kept;
    std::printf("selection=%s unlimited=%d limited=%d\n", verdict, unlimited, limited);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
