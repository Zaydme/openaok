#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;

entt::entity TownCenterOf(entt::registry& reg, Core::PlayerId player) {
    const int tc = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    auto view = reg.view<ECS::Components::Building, ECS::Components::Identity,
                         ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<ECS::Components::Identity>(e).unit_id == tc &&
            view.get<ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

} // namespace

// Fog of war, per player, in the simulation where the game can play from it.
bool CheckVision() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };

    const entt::entity ours = TownCenterOf(reg, kUs);
    const entt::entity theirs = TownCenterOf(reg, kThem);
    if (ours == entt::null || theirs == entt::null) verdict = "no town centers to see from";

    auto tile = [&](entt::entity e) {
        const auto& p = reg.get<ECS::Components::Transform>(e).position;
        return std::make_pair(Physics::TileOf(p.x), Physics::TileOf(p.y));
    };

    // Each sees its own base and not the other's.
    if (ok()) {
        const auto home = tile(ours);
        const auto away = tile(theirs);
        if (!ECS::Vision::Visible(reg, kUs, home.first, home.second)) {
            verdict = "a player cannot see its own town center";
        } else if (ECS::Vision::Explored(reg, kUs, away.first, away.second)) {
            verdict = "a player has explored the enemy base at the start";
        } else if (ECS::Vision::Shows(reg, kUs, theirs)) {
            verdict = "an unexplored enemy town center is shown";
        }
    }

    // A scout walks out: the ground it passes is seen, and stays explored
    // once it has moved on, though out of sight.
    entt::entity scout = entt::null;
    if (ok()) {
        const int scout_id = cfg.GetUnitIdByName("SCOUT");
        auto units = reg.view<ECS::Components::Identity, ECS::Components::Owner>();
        for (auto e : units) {
            if (units.get<ECS::Components::Identity>(e).unit_id == scout_id &&
                units.get<ECS::Components::Owner>(e).player_id == kUs) {
                scout = e;
            }
        }
        if (scout == entt::null) verdict = "the player has no scout";
    }
    if (ok()) {
        const auto start = reg.get<ECS::Components::Transform>(scout).position;
        // Out and back: twenty tiles away from home -- beyond even a Teuton
        // Town Center's sight -- then home again. Whichever way has open
        // ground that far out: a start can be near the map's edge, or a wood.
        static const int kWays[4][2] = {{-20, 0}, {20, 0}, {0, -20}, {0, 20}};
        Core::Fixed32 out_x = start.x;
        Core::Fixed32 out_y = start.y;
        for (const auto& way : kWays) {
            const Core::Fixed32 x = start.x + Core::Fixed32(way[0]);
            const Core::Fixed32 y = start.y + Core::Fixed32(way[1]);
            if (headless.GetGrid().IsWalkable(Physics::TileOf(x), Physics::TileOf(y))) {
                out_x = x;
                out_y = y;
                break;
            }
        }
        const int far_x = Physics::TileOf(out_x);
        const int far_y = Physics::TileOf(out_y);
        const bool seen_before = ECS::Vision::Explored(reg, kUs, far_x, far_y);
        Core::CommandQueue::Get().PushCommand(Core::MoveCommand{scout, out_x, out_y, kUs});
        for (int t = 0; t < 1800; ++t) headless.Update(16);
        const bool seen_there = ECS::Vision::Visible(reg, kUs, far_x, far_y);
        Core::CommandQueue::Get().PushCommand(
            Core::MoveCommand{scout, start.x, start.y, kUs});
        for (int t = 0; t < 1800; ++t) headless.Update(16);
        if (seen_before) {
            verdict = "ground twenty tiles out was explored before anyone went there";
        } else if (!seen_there) {
            verdict = "a scout standing somewhere could not see it";
        } else if (!ECS::Vision::Explored(reg, kUs, far_x, far_y)) {
            verdict = "ground a scout passed did not stay explored";
        } else if (ECS::Vision::Visible(reg, kUs, far_x, far_y)) {
            verdict = "ground stayed visible after the scout left it";
        }
    }

    // An enemy unit is never shown in the fog; an enemy building once found is,
    // as the picture we keep of it.
    if (ok()) {
        const auto away = tile(theirs);
        const entt::entity spy = ECS::Units::Spawn(
            reg, cfg.GetUnitIdByName("SCOUT"), kUs,
            {Core::Fixed32(away.first + 2), Core::Fixed32(away.second + 2)});
        const entt::entity guard = ECS::Units::Spawn(
            reg, cfg.GetUnitIdByName("VMBAS"), kThem,
            {Core::Fixed32(away.first + 3), Core::Fixed32(away.second + 2)});
        headless.Update(16);
        const bool saw_tc = ECS::Vision::Shows(reg, kUs, theirs);
        const bool saw_guard = ECS::Vision::Shows(reg, kUs, guard);
        reg.destroy(spy);
        headless.Update(16);
        bool pictured = false;
        auto pictures = reg.view<const ECS::Components::Doppelganger>();
        for (auto e : pictures) {
            const auto& d = pictures.get<const ECS::Components::Doppelganger>(e);
            pictured = pictured || (d.source == theirs && d.marked && ECS::Vision::Shows(reg, kUs, e));
        }
        if (!saw_tc || !saw_guard) {
            verdict = "a scout beside the enemy base could not see it";
        } else if (!pictured || ECS::Vision::Shows(reg, kUs, theirs)) {
            verdict = "an enemy building found once is not shown in the fog by its picture";
        } else if (ECS::Vision::Shows(reg, kUs, guard)) {
            verdict = "an enemy unit was shown in the fog";
        }
    }

    // "All visible" is all visible.
    if (ok()) {
        ECS::Simulation open;
        Core::GameSetup setup = Core::DefaultSetup();
        setup.reveal = Core::Reveal::AllVisible;
        open.Init(setup);
        const entt::entity far_tc = TownCenterOf(open.GetRegistry(), kThem);
        const auto& p = open.GetRegistry().get<ECS::Components::Transform>(far_tc).position;
        if (!ECS::Vision::Visible(open.GetRegistry(), kUs, Physics::TileOf(p.x),
                                  Physics::TileOf(p.y))) {
            verdict = "with everything visible, the enemy base was not";
        }
    }

    std::printf("vision=%s\n", verdict);
    return ok();
}

} // namespace Verify
