#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdio>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/Profile.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

constexpr int kLoom = 22;
constexpr int kTicks = 200;   // of the Town Center's Loom, a few seconds

} // namespace

// The Town Center researching Loom with two villagers behind it, a Barracks
// training two militia: the overview shows Loom first, partly done, then two
// villagers and two militia; and takes room over the map only while the
// Options' Production Queue is on.
bool CheckQueueOverview() {
    using UI = Rendering::UIRenderer;
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
    const entt::entity barracks = ECS::Buildings::Place(reg, grid, cfg.GetUnitIdByName("BRKS"), kUs, sx + 8, sy, true);
    const int villager = cfg.GetUnitIdByName("VMBAS");
    const int militia = cfg.GetUnitIdByName("SPRMN");
    UI::QueueEntry entries[UI::kMaxQueueEntries];
    int n = 0;
    int shown_on = 0, shown_off = 0;
    if (centre == entt::null || barracks == entt::null) {
        verdict = "no Town Center or Barracks";
    } else {
        ECS::Orders::Execute(reg, grid, Core::ResearchCommand{centre, kLoom, kUs});
        for (int i = 0; i < 2; ++i) {
            ECS::Orders::Execute(reg, grid, Core::TrainCommand{centre, villager, kUs});
            ECS::Orders::Execute(reg, grid, Core::TrainCommand{barracks, militia, kUs});
        }
        for (int t = 0; t < kTicks; ++t) sim.Update(16);
        n = UI::QueueOverview(reg, entries, UI::kMaxQueueEntries);
        if (n != 3) {
            verdict = "the overview does not hold Loom, the villagers and the militia";
        } else if (!entries[0].tech || entries[0].id != kLoom || entries[0].total <= 0 || entries[0].done <= 0 ||
                   entries[0].done >= entries[0].total) {
            verdict = "Loom is not first, partly done";
        } else if (entries[1].tech || entries[1].id != villager || entries[1].count != 2 ||
                   entries[2].tech || entries[2].id != militia || entries[2].count != 2) {
            verdict = "the units are not counted by kind, in their buildings' order";
        }
        // Drawn: room over the map while the option is on, none while off.
        SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
        auto& profile = Core::Profile::Current();
        const bool was = profile.production_queue;
        if (r != nullptr) {
            profile.production_queue = true;
            shown_on = UI::DrawQueueOverview(sim, r, 1024, 768);
            profile.production_queue = false;
            shown_off = UI::DrawQueueOverview(sim, r, 1024, 768);
        }
        profile.production_queue = was;
        if (verdict == nullptr && (shown_on <= 0 || shown_off != 0)) {
            verdict = "the overview does not follow the Options' Production Queue";
        }
        if (r != nullptr) SDL_DestroyRenderer(r);
        if (screen != nullptr) SDL_FreeSurface(screen);
    }
    std::printf("queueoverview=%s entries=%d on=%d off=%d\n", verdict == nullptr ? "ok" : verdict, n, shown_on,
                shown_off);
    return verdict == nullptr;
}

} // namespace Verify
