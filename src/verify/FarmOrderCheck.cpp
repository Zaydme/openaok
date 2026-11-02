#include "verify/Harness.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <variant>
#include <vector>

#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/InputManager.h"
#include "core/IsometricMath.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Building.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Renderer.h"

namespace Verify {

namespace {

constexpr int kFrameW = 1024;
constexpr int kFrameH = 768;

// Whether a gather order at `farm` took: the villager's job is that farm.
bool Takes(ECS::Simulation& sim, entt::entity villager, entt::entity farm) {
    auto& reg = sim.GetRegistry();
    ECS::Orders::Execute(reg, sim.GetGrid(), Core::GatherCommand{villager, farm, 1});
    return reg.get<ECS::Components::Worker>(villager).target_resource == farm;
}

// A right-click at `at`, the villager selected: whether it gave a gather
// order and whether an attack order, at `target`.
void RightClick(ECS::Simulation& sim, const ECS::Components::Vector2D& at, entt::entity target, bool& gathered,
                bool& attacked) {
    Rendering::Renderer view;
    view.view_w = static_cast<float>(kFrameW);
    view.view_h = static_cast<float>(kFrameH);
    const SDL_FPoint p = Core::IsometricMath::WorldToScreen(at.x, at.y);
    view.camera_x = view.target_camera_x = p.x - view.view_w / 2.0f;
    view.camera_y = view.target_camera_y = p.y - view.view_h / 2.0f;
    auto& input = Core::InputManager::Get();
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
        click.button.button = SDL_BUTTON_RIGHT;
        click.button.x = kFrameW / 2;
        click.button.y = kFrameH / 2;
        input.ProcessEvent(click, sim, view);
    }
    gathered = attacked = false;
    for (const Core::Command& c : Core::CommandQueue::Get().PopAllCommands()) {
        if (const auto* g = std::get_if<Core::GatherCommand>(&c)) gathered |= g->target_resource == target;
        if (const auto* a = std::get_if<Core::AttackCommand>(&c)) attacked |= a->target_entity == target;
    }
}

} // namespace

// A Farm is worked only once it stands and only by its owner: a villager of
// player 1's sent to gather at player 2's Farm, or at its own Farm still a
// foundation, is refused, and at its own finished Farm goes; a right-click
// on player 2's Farm strikes it, as the cursor there shows.
bool CheckFarmOrder() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const int farm_id = cfg.GetUnitIdByName("FARM");
    // Three Farms in a row on open ground: player 2's, then player 1's
    // foundation and its finished one.
    int fx = -1, fy = -1;
    for (int y = grid.GetHeight() / 3; y < grid.GetHeight() - 12 && fx < 0; ++y) {
        for (int x = grid.GetWidth() / 3; x < grid.GetWidth() - 20 && fx < 0; ++x) {
            bool open = true;
            for (int oy = -4; oy <= 8 && open; ++oy) {
                for (int ox = -4; ox <= 16 && open; ++ox) open = grid.IsWalkable(x + ox, y + oy);
            }
            if (open) {
                fx = x;
                fy = y;
            }
        }
    }
    entt::entity theirs = entt::null, unbuilt = entt::null, ours = entt::null, villager = entt::null;
    if (fx < 0) {
        verdict = "no open ground for the farms";
    } else {
        // Nothing else on that ground: no animal, no bush, no unit.
        std::vector<entt::entity> there;
        auto things = reg.view<const ECS::Components::Transform>();
        for (auto e : things) {
            if (reg.any_of<ECS::Components::Building, ECS::Components::PartOf>(e)) continue;
            const auto& p = things.get<const ECS::Components::Transform>(e).position;
            const int x = Physics::TileOf(p.x), y = Physics::TileOf(p.y);
            if (x >= fx - 6 && x <= fx + 18 && y >= fy - 6 && y <= fy + 10) there.push_back(e);
        }
        for (entt::entity e : there) reg.destroy(e);
        theirs = ECS::Buildings::Place(reg, grid, farm_id, 2, fx, fy, true);
        unbuilt = ECS::Buildings::Place(reg, grid, farm_id, 1, fx + 5, fy, false);
        ours = ECS::Buildings::Place(reg, grid, farm_id, 1, fx + 10, fy, true);
        villager = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), 1,
                                             {Core::Fixed32(fx + 1), Core::Fixed32(fy + 3)}, {}, 0);
        if (theirs == entt::null || unbuilt == entt::null || ours == entt::null || villager == entt::null) {
            verdict = "the farms or the villager could not be placed";
        }
    }
    bool gathered = false, attacked = false;
    if (std::strcmp(verdict, "ok") == 0) {
        for (int t = 0; t < 30; ++t) sim.Update(16);
        auto all = reg.view<ECS::Components::Selectable>();
        for (auto e : all) all.get<ECS::Components::Selectable>(e).is_selected = e == villager;
        // A frame with nothing in sight, so the click aims at the ground.
        SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* r = screen != nullptr ? SDL_CreateSoftwareRenderer(screen) : nullptr;
        Rendering::Renderer away;
        away.camera_x = away.camera_y = -1.0e6f;
        if (r != nullptr) Rendering::EntityRenderer::DrawGroundLevel(sim, away, r, 1.0f);
        RightClick(sim, reg.get<ECS::Components::Transform>(theirs).position, theirs, gathered, attacked);
        if (r != nullptr) SDL_DestroyRenderer(r);
        if (screen != nullptr) SDL_FreeSurface(screen);
        if (gathered) {
            verdict = "a right-click on another player's Farm sent the villager to farm it";
        } else if (!attacked) {
            verdict = "a right-click on another player's Farm did not strike it";
        }
    }
    if (std::strcmp(verdict, "ok") == 0) {
        if (Takes(sim, villager, theirs)) {
            verdict = "a villager went to farm another player's Farm";
        } else if (Takes(sim, villager, unbuilt)) {
            verdict = "a villager went to farm a Farm not yet built";
        } else if (!Takes(sim, villager, ours)) {
            verdict = "a villager would not farm its own Farm";
        }
    }
    std::printf("farmorder=%s gathered=%d attacked=%d\n", verdict, gathered ? 1 : 0, attacked ? 1 : 0);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
