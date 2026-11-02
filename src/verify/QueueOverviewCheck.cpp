#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "core/Profile.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "rendering/Renderer.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

using UI = Rendering::UIRenderer;
constexpr int kLoom = 22;
constexpr int kTicks = 200;   // of the Town Center's Loom, a few seconds
constexpr int kFrameW = 1024;
constexpr int kFrameH = 768;

bool Is(const UI::QueueEntry& e, bool tech, int id, bool waiting, int count) {
    return e.tech == tech && e.id == id && e.waiting == waiting && e.count == count;
}

// The one building selected, or null.
entt::entity Selected(entt::registry& registry) {
    entt::entity one = entt::null;
    auto all = registry.view<ECS::Components::Selectable>();
    for (auto e : all) {
        if (!all.get<ECS::Components::Selectable>(e).is_selected) continue;
        if (one != entt::null) return entt::null;
        one = e;
    }
    return one;
}

// A left click at the middle of `cell`.
void Click(ECS::Simulation& sim, Rendering::Renderer& view, const SDL_Rect& cell) {
    auto& input = Core::InputManager::Get();
    for (const Uint32 type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
        SDL_Event e{};
        e.type = type;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = cell.x + cell.w / 2;
        e.button.y = cell.y + cell.h / 2;
        input.ProcessEvent(e, sim, view);
    }
}

} // namespace

// The Town Center told to train two villagers, research Loom and train one
// more, two Barracks each two militia: the overview shows what leads the
// queues -- two villagers, partly done, and four militia -- then what waits
// behind something else -- Loom and one villager; a Barracks held up for want
// of houses has its militia waiting too. Clicking the militia selects one
// Barracks, then the other, then the first again, the view zoomed out centred
// on each. What is done greens an icon from the left by its share. The
// overview shows, and takes clicks, only while the Options' Production Queue
// is on.
bool CheckQueueOverview() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    constexpr Core::PlayerId kUs = 1;
    if (ECS::Players::Player* us = ECS::Players::Find(reg, kUs)) {
        for (auto type : {Core::ResourceType::Food, Core::ResourceType::Wood, Core::ResourceType::Gold,
                          Core::ResourceType::Stone}) {
            ECS::Players::AddStock(*us, type, 10000);
        }
    }
    entt::entity centre = entt::null;
    auto buildings = reg.view<ECS::Components::Building, ECS::Components::Identity>();
    for (auto b : buildings) {
        if (buildings.get<ECS::Components::Identity>(b).unit_id == cfg.GetUnitIdByName("RTWC")) centre = b;
    }
    const auto [sx, sy] = sim.player_starts[0];
    const int barracks_id = cfg.GetUnitIdByName("BRKS");
    const entt::entity barracks[2] = {ECS::Buildings::Place(reg, grid, barracks_id, kUs, sx + 8, sy, true),
                                      ECS::Buildings::Place(reg, grid, barracks_id, kUs, sx - 8, sy, true)};
    // Room for all of them to be trained at once.
    for (int i = 0; i < 2; ++i) {
        ECS::Buildings::Place(reg, grid, cfg.GetUnitIdByName("HOUS"), kUs, sx - 3 + 3 * i, sy + 8, true);
    }
    const int villager = cfg.GetUnitIdByName("VMBAS");
    const int militia = cfg.GetUnitIdByName("SPRMN");
    UI::QueueEntry entries[UI::kMaxQueueEntries];
    int n = 0, shown_on = 0, shown_off = 0;
    auto& profile = Core::Profile::Current();
    const bool was = profile.production_queue;
    profile.production_queue = true;
    if (centre == entt::null || barracks[0] == entt::null || barracks[1] == entt::null) {
        verdict = "no Town Center or Barracks";
    } else {
        for (int i = 0; i < 2; ++i) {
            ECS::Orders::Execute(reg, grid, Core::TrainCommand{centre, villager, kUs});
            for (entt::entity b : barracks) ECS::Orders::Execute(reg, grid, Core::TrainCommand{b, militia, kUs});
        }
        ECS::Orders::Execute(reg, grid, Core::ResearchCommand{centre, kLoom, kUs});
        ECS::Orders::Execute(reg, grid, Core::TrainCommand{centre, villager, kUs});
        for (int t = 0; t < kTicks; ++t) sim.Update(16);
        n = UI::QueueOverview(reg, entries, UI::kMaxQueueEntries);
        if (n != 4) {
            verdict = "the overview does not hold the villagers and militia leading and what waits";
        } else if (!Is(entries[0], false, villager, false, 2) || entries[0].total <= 0 || entries[0].done <= 0 ||
                   entries[0].done >= entries[0].total) {
            verdict = "the villagers leading their queue are not first, partly done";
        } else if (!Is(entries[1], false, militia, false, 4) || !Is(entries[2], true, kLoom, true, 1) ||
                   !Is(entries[3], false, villager, true, 1)) {
            verdict = "what leads and what waits behind something else are not counted by kind, in that order";
        }

        // A Barracks held up for want of houses: its militia wait, yellowed.
        if (verdict == nullptr) {
            auto& held = reg.get<ECS::Components::ProductionQueue>(barracks[1]);
            const bool was_housed = held.housed;
            held.housed = true;
            UI::QueueEntry stalled[UI::kMaxQueueEntries];
            const int m = UI::QueueOverview(reg, stalled, UI::kMaxQueueEntries);
            held.housed = was_housed;
            if (m != 5 || !Is(stalled[1], false, militia, false, 2) || !Is(stalled[4], false, militia, true, 2)) {
                verdict = "a queue held up for want of houses is not all waiting";
            }
        }

        // Clicks on the militia being made: each Barracks in turn.
        Rendering::Renderer view;
        view.view_w = static_cast<float>(kFrameW);
        view.view_h = static_cast<float>(kFrameH);
        view.camera_zoom = view.target_camera_zoom = 0.5f;
        auto& input = Core::InputManager::Get();
        input.Update(view, 0.0f, kFrameW, kFrameH);
        const SDL_Rect cell = UI::QueueEntryRect(1, kFrameW, kFrameH);
        entt::entity picked[3] = {entt::null, entt::null, entt::null};
        bool centred = true;
        for (entt::entity& p : picked) {
            Click(sim, view, cell);
            p = Selected(reg);
            // The view, zoomed out to half size, centred on it.
            if (p == entt::null) continue;
            const auto& at = reg.get<ECS::Components::Transform>(p).position;
            const SDL_FPoint s = Core::IsometricMath::WorldToScreen(at.x, at.y);
            const float mid_x = view.camera_x + kFrameW / 2.0f / view.camera_zoom;
            const float mid_y = view.camera_y + kFrameH / 2.0f / view.camera_zoom;
            if (std::fabs(mid_x - s.x) > 1.0f || std::fabs(mid_y - s.y) > 1.0f) centred = false;
        }
        const bool both = (picked[0] == barracks[0] && picked[1] == barracks[1]) ||
                          (picked[0] == barracks[1] && picked[1] == barracks[0]);
        if (verdict == nullptr && (!both || picked[2] != picked[0])) {
            verdict = "clicking an entry does not go round the buildings making it";
        } else if (verdict == nullptr && !centred) {
            verdict = "the view zoomed out is not centred on the building an entry goes to";
        }

        // What is done greens an icon from the left by its share.
        const SDL_Rect icon{10, 20, 36, 36};
        const SDL_Rect quarter = UI::ProgressFill(icon, 9, 36);
        const SDL_Rect none = UI::ProgressFill(icon, 0, 36);
        const SDL_Rect whole = UI::ProgressFill(icon, 40, 36);
        if (verdict == nullptr &&
            (quarter.x != 10 || quarter.y != 20 || quarter.w != 9 || quarter.h != 36 || none.w != 0 ||
             whole.w != 36)) {
            verdict = "what is done does not green an icon from the left by its share";
        }

        // Drawn: room over the map while the option is on, none while off,
        // and no entry to click.
        SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, kFrameW, kFrameH, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
        UI::QueueEntry unused;
        int hit_off = 0;
        if (r != nullptr) {
            shown_on = UI::DrawQueueOverview(sim, r, kFrameW, kFrameH);
            profile.production_queue = false;
            shown_off = UI::DrawQueueOverview(sim, r, kFrameW, kFrameH);
            hit_off = UI::QueueEntryAt(sim, kFrameW, kFrameH, cell.x + 1, cell.y + 1, unused);
        }
        if (verdict == nullptr && (shown_on <= 0 || shown_off != 0 || hit_off != -1)) {
            verdict = "the overview does not follow the Options' Production Queue";
        }
        if (r != nullptr) SDL_DestroyRenderer(r);
        if (screen != nullptr) SDL_FreeSurface(screen);
    }
    profile.production_queue = was;
    std::printf("queueoverview=%s entries=%d on=%d off=%d\n", verdict == nullptr ? "ok" : verdict, n, shown_on,
                shown_off);
    return verdict == nullptr;
}

} // namespace Verify
