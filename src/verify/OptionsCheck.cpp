#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <variant>

#include "audio/Notices.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "core/Profile.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Selectable.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Renderer.h"

namespace Verify {

namespace {

constexpr int kFrameW = 1024;
constexpr int kFrameH = 768;

// A left click at the middle of the frame, over open ground beside the
// villager, with the Options' mouse interface `two_button`: how many move
// orders it gave.
int MovesFromLeftClick(ECS::Simulation& sim, const ECS::Components::Vector2D& at, bool two_button) {
    using Input = Core::InputManager;
    auto& profile = Core::Profile::Current();
    profile.two_button_mouse = two_button;
    Rendering::Renderer view;
    view.view_w = static_cast<float>(kFrameW);
    view.view_h = static_cast<float>(kFrameH);
    const SDL_FPoint p = Core::IsometricMath::WorldToScreen(at.x, at.y);
    view.camera_x = view.target_camera_x = p.x - view.view_w / 2.0f;
    view.camera_y = view.target_camera_y = p.y - view.view_h / 2.0f;
    auto& input = Input::Get();
    input.Update(view, 0.0f, kFrameW, kFrameH);
    Core::CommandQueue::Get().PopAllCommands();
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = kFrameW / 2;
    e.motion.y = kFrameH / 2;
    input.ProcessEvent(e, sim, view);
    input.Hover(sim, view);
    for (const Uint32 type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
        SDL_Event click{};
        click.type = type;
        click.button.button = SDL_BUTTON_LEFT;
        click.button.x = kFrameW / 2;
        click.button.y = kFrameH / 2;
        input.ProcessEvent(click, sim, view);
    }
    int moves = 0;
    for (const Core::Command& c : Core::CommandQueue::Get().PopAllCommands()) {
        if (std::holds_alternative<Core::MoveCommand>(c)) ++moves;
    }
    return moves;
}

} // namespace

// The Options that change how the game is played: Scroll Speed scrolls the
// map 800 screen pixels a second at its middle setting, a quarter as fast at
// its slowest and four times at its fastest; with Allow Audio Taunts off a
// taunt plays no sound; with one mouse button a left click over open ground,
// a villager selected, orders it there, where with two it does not.
bool CheckOptions() {
    using Input = Core::InputManager;
    const char* verdict = nullptr;
    auto& profile = Core::Profile::Current();
    const Core::Profile::Options was = profile;

    const float slow = Input::PanPerSecond(0), middle = Input::PanPerSecond(5), fast = Input::PanPerSecond(10);
    if (std::fabs(middle - 800.0f) > 0.5f || std::fabs(slow - 200.0f) > 0.5f || std::fabs(fast - 3200.0f) > 0.5f) {
        verdict = "the map does not scroll at the Options' Scroll Speed";
    }

    profile.audio_taunts = true;
    const bool heard = Audio::Notices::PlaysTaunt(11);
    profile.audio_taunts = false;
    const bool unheard = !Audio::Notices::PlaysTaunt(11);
    if (verdict == nullptr && !(heard && unheard)) verdict = "a taunt's sound does not follow Allow Audio Taunts";

    int one = -1, two = -1;
    if (verdict == nullptr) {
        ECS::Simulation sim;
        sim.Init();
        auto& reg = sim.GetRegistry();
        sim.Update(16);
        const auto [sx, sy] = sim.player_starts[0];
        const entt::entity villager = ECS::ScenarioWorld::Place(
            reg, sim.GetGrid(), Core::ConfigManager::Get().GetUnitIdByName("VMBAS"), 1,
            {Core::Fixed32(sx + 6), Core::Fixed32(sy + 6)}, {}, 0);
        sim.Update(16);
        if (villager == entt::null) {
            verdict = "no villager placed";
        } else {
            auto all = reg.view<ECS::Components::Selectable>();
            for (auto e : all) all.get<ECS::Components::Selectable>(e).is_selected = e == villager;
            // A frame with nothing in sight, so the ground is open to the pointer.
            SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
            SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
            Rendering::Renderer away;
            away.camera_x = away.camera_y = -1.0e6f;
            if (r != nullptr) Rendering::EntityRenderer::DrawGroundLevel(sim, away, r, 1.0f);
            const ECS::Components::Vector2D open{Core::Fixed32(sx + 9), Core::Fixed32(sy + 6)};
            one = MovesFromLeftClick(sim, open, false);
            two = MovesFromLeftClick(sim, open, true);
            if (one != 1 || two != 0) verdict = "a left click does not follow the Options' mouse interface";
            if (r != nullptr) SDL_DestroyRenderer(r);
            if (screen != nullptr) SDL_FreeSurface(screen);
        }
    }
    profile = was;
    std::printf("options=%s scroll=%.0f/%.0f/%.0f oneclick=%d twoclick=%d\n", verdict == nullptr ? "ok" : verdict,
                slow, middle, fast, one, two);
    return verdict == nullptr;
}

} // namespace Verify
