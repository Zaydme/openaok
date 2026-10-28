#include "verify/Harness.h"

#include <cstdio>
#include <string>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/DropSites.h"
#include "ecs/Garrisons.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
namespace R = Core::Rules;
using Core::Fixed32;
constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;
constexpr int kTicksPerSecond = 1000 / 16;

entt::entity Find(entt::registry& reg, Core::PlayerId player, int unit_id) {
    auto view = reg.view<C::Identity, C::Owner>();
    for (auto e : view) {
        if (view.get<C::Identity>(e).unit_id == unit_id && view.get<C::Owner>(e).player_id == player) return e;
    }
    return entt::null;
}

Fixed32 Stock(entt::registry& reg, int dat_resource) {
    return ECS::Players::Resource(*ECS::Players::Find(reg, kUs), dat_resource);
}

// Clears a villager's hands, then gives it `whole` of `type` and `part` of a
// unit gathered toward the next.
void Load(C::Worker& worker, Core::ResourceType type, int whole, Fixed32 part) {
    for (int& held : worker.carried) held = 0;
    worker.carry_type = type;
    worker.carried[static_cast<int>(type)] = whole;
    worker.gather_progress = part;
}

} // namespace

bool CheckDropoff() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    std::string verdict = "ok";
    auto ok = [&]() { return verdict == "ok"; };
    auto run = [&](int ticks) {
        for (int t = 0; t < ticks; ++t) sim.Update(16);
    };
    sim.Update(16);

    const int town_center = cfg.GetUnitIdByName("RTWC");
    const int mill = cfg.GetUnitIdByName("MILL");
    const int tower = cfg.GetUnitIdByName("WCTW");
    const entt::entity tc = Find(reg, kUs, town_center);
    entt::entity villager = entt::null;
    auto workers = reg.view<C::Worker, C::Owner>();
    for (auto e : workers) {
        if (villager == entt::null && workers.get<C::Owner>(e).player_id == kUs) villager = e;
    }
    Core::PlayerId them = 0;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers && them == 0; ++p) {
        if (p != kUs && ECS::Players::Find(reg, p) != nullptr) them = p;
    }
    const entt::entity theirs = them != 0 ? Find(reg, them, town_center) : entt::null;
    if (tc == entt::null || villager == entt::null || theirs == entt::null) verdict = "no Town Centers or villager";
    const auto home = ok() ? reg.get<C::Transform>(tc).position : C::Vector2D{};

    // Somewhere open beside the Town Center for a Mill, a foundation and a tower.
    auto place = [&](int unit_id, bool complete) {
        for (int r = 4; r < 20; ++r) {
            for (int d = -r; d <= r; ++d) {
                const int x = home.x.ToInt() + d, y = home.y.ToInt() + r;
                if (ECS::Buildings::CanPlace(grid, unit_id, x, y)) {
                    return ECS::Buildings::Place(reg, grid, unit_id, kUs, x, y, complete);
                }
            }
        }
        return entt::entity{entt::null};
    };

    // Cutting a tree, sent with a few wood and part of the next to the Town
    // Center: it hands all of it over, unrounded, and stands, rather than
    // going back to the tree.
    if (ok()) {
        entt::entity tree = entt::null;
        Fixed32 nearest{};
        auto woods = reg.view<C::Resource, C::Transform>();
        for (auto e : woods) {
            if (woods.get<C::Resource>(e).type != Core::ResourceType::Wood) continue;
            const auto& at = woods.get<C::Transform>(e).position;
            const Fixed32 d = Core::FixedMath::Distance(at.x - home.x, at.y - home.y);
            if (tree == entt::null || d < nearest) {
                tree = e;
                nearest = d;
            }
        }
        if (tree != entt::null) ECS::Orders::Execute(reg, grid, Core::GatherCommand{villager, tree, kUs});
        const int wood = static_cast<int>(Core::ResourceType::Wood);
        for (int t = 0; t < 90 * kTicksPerSecond && reg.get<C::Worker>(villager).carried[wood] < 3; ++t) {
            sim.Update(16);
        }
        auto& worker = reg.get<C::Worker>(villager);
        const Fixed32 held = Fixed32(worker.carried[wood]) + worker.gather_progress;
        if (tree == entt::null || worker.carried[wood] < 3 || worker.carried[wood] >= worker.max_carry) {
            verdict = "a villager did not cut a few wood to drop off";
        }
        const Fixed32 before = Stock(reg, R::Res::Wood);
        ECS::Orders::Execute(reg, grid, Core::DropOffCommand{villager, tc, kUs});
        bool done = false;
        for (int t = 0; t < 30 * kTicksPerSecond && !done; ++t) {
            sim.Update(16);
            done = reg.get<C::Worker>(villager).carried[static_cast<int>(Core::ResourceType::Wood)] == 0;
        }
        const auto stood = reg.get<C::Transform>(villager).position;
        run(3 * kTicksPerSecond);
        const auto& after = reg.get<C::Worker>(villager);
        const auto& now = reg.get<C::Transform>(villager).position;
        if (!ok()) {
        } else if (!done) {
            verdict = "a villager sent to drop off never handed its load over";
        } else if (Stock(reg, R::Res::Wood) != before + held) {
            verdict = "a drop-off did not hand over the load and its part of a unit";
        } else if (after.gather_progress.value != 0) {
            verdict = "a drop-off kept the part of a unit in hand";
        } else if (after.state != Core::UnitState::Idle || reg.valid(after.target_resource) ||
                   now.x != stood.x || now.y != stood.y) {
            verdict = "a villager that dropped off by order did not stand";
        }
    }

    // However little: a part of a unit alone is enough to drop off.
    if (ok()) {
        auto& worker = reg.get<C::Worker>(villager);
        Load(worker, Core::ResourceType::Food, 0, Fixed32(1) / Fixed32(2));
        const Fixed32 before = Stock(reg, R::Res::Food);
        if (!ECS::DropSites::Takes(reg, villager, tc)) {
            verdict = "half a unit of food in hand is not taken at the Town Center";
        } else {
            ECS::Orders::Execute(reg, grid, Core::DropOffCommand{villager, tc, kUs});
            run(10 * kTicksPerSecond);
            if (Stock(reg, R::Res::Food) != before + Fixed32(1) / Fixed32(2)) {
                verdict = "half a unit of food was not dropped off";
            }
        }
    }

    // Only one of the player's own finished buildings that takes the load.
    if (ok()) {
        auto& worker = reg.get<C::Worker>(villager);
        Load(worker, Core::ResourceType::Wood, 5, Fixed32(0));
        const entt::entity built = place(mill, true);
        const entt::entity site = place(mill, false);
        if (built == entt::null || site == entt::null) verdict = "no ground for the Mills";
        else if (ECS::DropSites::Takes(reg, villager, built)) verdict = "a Mill takes wood";
        else if (ECS::DropSites::Takes(reg, villager, theirs)) verdict = "another player's Town Center takes a drop-off";
        if (ok()) {
            Load(worker, Core::ResourceType::Food, 5, Fixed32(0));
            if (ECS::DropSites::Takes(reg, villager, site)) verdict = "a Mill not yet built takes food";
            else if (!ECS::DropSites::Takes(reg, villager, built)) verdict = "a Mill does not take food";
        }
        if (ok()) {
            Load(worker, Core::ResourceType::Wood, 5, Fixed32(0));
            ECS::Orders::Execute(reg, grid, Core::DropOffCommand{villager, built, kUs});
            if (reg.get<C::Worker>(villager).state == Core::UnitState::Returning) {
                verdict = "a villager was sent to drop wood off at a Mill";
            }
        }
    }

    // Going into the Town Center banks what it carries, to the nearest whole
    // unit; going into a tower banks nothing.
    if (ok()) {
        auto& worker = reg.get<C::Worker>(villager);
        Load(worker, Core::ResourceType::Food, 2, Fixed32(3) / Fixed32(5));
        const Fixed32 before = Stock(reg, R::Res::Food);
        ECS::Garrisons::PutInside(reg, villager, tc);
        if (Stock(reg, R::Res::Food) != before + Fixed32(3)) {
            verdict = "a villager going into the Town Center did not bank its load, rounded";
        }
        ECS::Orders::Execute(reg, grid, Core::UngarrisonCommand{tc, kUs});
        run(kTicksPerSecond);
    }
    if (ok()) {
        const entt::entity watch = place(tower, true);
        auto& worker = reg.get<C::Worker>(villager);
        Load(worker, Core::ResourceType::Food, 4, Fixed32(0));
        const Fixed32 before = Stock(reg, R::Res::Food);
        if (watch == entt::null) {
            verdict = "no ground for a tower";
        } else {
            ECS::Garrisons::PutInside(reg, villager, watch);
            if (Stock(reg, R::Res::Food) != before ||
                reg.get<C::Worker>(villager).carried[static_cast<int>(Core::ResourceType::Food)] != 4) {
                verdict = "a villager going into a tower banked its load";
            }
        }
    }

    std::printf("dropoff=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
