#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr int kTicksPerSecond = 1000 / 16;

ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2),
            Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)};
}

// Open ground `half` tiles each way round the tile nearest the map's middle.
bool Clearing(const Physics::Grid& grid, int& x, int& y, int half) {
    for (int radius = 0; radius < 60; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                bool clear = true;
                for (int oy = -half; oy <= half && clear; ++oy) {
                    for (int ox = -half; ox <= half && clear; ++ox) {
                        if (!grid.IsWalkable(x + dx + ox, y + dy + oy)) clear = false;
                    }
                }
                if (clear) {
                    x += dx;
                    y += dy;
                    return true;
                }
            }
        }
    }
    return false;
}

// Gaia's animals within `reach` tiles of the tile go, so none joins in.
void Quieten(entt::registry& reg, int x, int y, int reach) {
    std::vector<entt::entity> wild;
    auto view = reg.view<const ECS::Components::Combat, const ECS::Components::Owner,
                         const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id != Core::PLAYER_GAIA) continue;
        const auto& at = view.get<const ECS::Components::Transform>(e).position;
        const int dx = at.x.ToInt() - x, dy = at.y.ToInt() - y;
        if (dx * dx + dy * dy < reach * reach) wild.push_back(e);
    }
    for (entt::entity e : wild) reg.destroy(e);
}

// What a villager went back to after breaking off its job.
struct After {
    Core::UnitState state = Core::UnitState::Idle;
    entt::entity building = entt::null;
};

// A villager of ours that has gathered at a berry bush is set to raise a
// Barracks, then struck by a wolf of one hit point, which it kills: what it
// is doing once the fight is over. (A repairer struck keeps mending.)
After AfterAFight() {
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    After out;
    int x = grid.GetWidth() / 2, y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 8)) return out;
    Quieten(reg, x, y, 30);
    const entt::entity bush = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("FORAG"),
                                                        Core::PLAYER_GAIA, At(x, y), {}, 0);
    const entt::entity villager = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), kUs,
                                                            At(x + 1, y), {}, 0);
    const int barracks = cfg.GetUnitIdByName("BRKS");
    entt::entity building = entt::null;
    for (int dx = 4; dx < 8 && building == entt::null; ++dx) {
        for (int dy = -3; dy <= 3 && building == entt::null; ++dy) {
            if (ECS::Buildings::CanPlace(grid, barracks, x + dx, y + dy)) {
                building = ECS::Buildings::Place(reg, grid, barracks, kUs, x + dx, y + dy, false);
            }
        }
    }
    if (bush == entt::null || villager == entt::null || building == entt::null) return out;
    ECS::Orders::Execute(reg, grid, Core::GatherCommand{villager, bush, kUs});
    for (int t = 0; t < 4 * kTicksPerSecond; ++t) sim.Update(16);
    ECS::Orders::Execute(reg, grid, Core::RepairCommand{{villager}, building, kUs});
    for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
    const auto& at = reg.get<ECS::Components::Transform>(villager).position;
    const entt::entity wolf = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("WOLFX"), Core::PLAYER_GAIA,
                                                        {at.x + Core::Fixed32(1), at.y}, {}, 0);
    if (wolf == entt::null) return out;
    reg.get<ECS::Components::Combat>(wolf).stance = ECS::Components::Stance::NoAttack;
    reg.get<ECS::Components::Health>(wolf).current_hp = 1;
    ECS::Combat::Hurt(reg, grid, villager, Core::Fixed32(1), wolf);
    // The fight, and a moment after it to take up the job again.
    for (int t = 0; t < 15 * kTicksPerSecond; ++t) sim.Update(16);
    const auto& worker = reg.get<ECS::Components::Worker>(villager);
    out.state = worker.state;
    out.building = worker.target_building == building ? building : entt::null;
    return out;
}

// A villager of ours mending a House by its Town Center when the town bell
// rings, and again once it is inside: what it is doing then.
After AfterTheBell() {
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    After out;
    entt::entity tc = entt::null;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == cfg.GetUnitIdByName("RTWC") &&
            view.get<const ECS::Components::Owner>(e).player_id == kUs) {
            tc = e;
        }
    }
    if (tc == entt::null) return out;
    const auto& home = reg.get<ECS::Components::Transform>(tc).position;
    const int house_id = cfg.GetUnitIdByName("HOUS");
    entt::entity house = entt::null;
    for (int r = 4; r < 14 && house == entt::null; ++r) {
        for (int d = -r; d <= r && house == entt::null; ++d) {
            const int x = Physics::TileOf(home.x) + d, y = Physics::TileOf(home.y) + r;
            if (ECS::Buildings::CanPlace(grid, house_id, x, y)) {
                house = ECS::Buildings::Place(reg, grid, house_id, kUs, x, y, true);
            }
        }
    }
    if (house == entt::null) return out;
    auto& health = reg.get<ECS::Components::Health>(house);
    health.current_hp = health.max_hp / 4;
    const auto& by = reg.get<ECS::Components::Transform>(house).position;
    const entt::entity villager = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), kUs,
                                                            {by.x + Core::Fixed32(3), by.y}, {}, 0);
    if (villager == entt::null) return out;
    ECS::Orders::Execute(reg, grid, Core::RepairCommand{{villager}, house, kUs});
    for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
    ECS::Orders::Execute(reg, grid, Core::TownBellCommand{tc, kUs});
    for (int t = 0; t < 30 * kTicksPerSecond && reg.all_of<ECS::Components::Transform>(villager); ++t) {
        sim.Update(16);
    }
    if (reg.all_of<ECS::Components::Transform>(villager)) return out;   // never went in
    ECS::Orders::Execute(reg, grid, Core::TownBellCommand{tc, kUs});
    sim.Update(16);
    const auto& worker = reg.get<ECS::Components::Worker>(villager);
    out.state = worker.state;
    out.building = worker.target_building == house ? house : entt::null;
    return out;
}

} // namespace

// A builder that breaks off for a fight, or a repairer for the town bell,
// goes back to the building it was at, not to a resource it gathered
// before.
bool CheckBackToWork() {
    const char* verdict = "ok";
    const After built = AfterAFight();
    const After belled = AfterTheBell();
    if (built.state != Core::UnitState::Building || built.building == entt::null) {
        verdict = "a builder that fought did not go back to its site";
    } else if (belled.state != Core::UnitState::Repairing || belled.building == entt::null) {
        verdict = "a repairer the bell called in did not go back to mending";
    }
    std::printf("backtowork=%s built=%d belled=%d\n", verdict, static_cast<int>(built.state),
                static_cast<int>(belled.state));
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
