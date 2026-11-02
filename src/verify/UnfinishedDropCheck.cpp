#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// A lumberjack of player 1's at a tree far from its Town Center, with a
// Lumber Camp foundation laid beside the tree and never built: its wood goes
// to the Town Center, never to the foundation.
bool CheckUnfinishedDrop() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const auto [sx, sy] = sim.player_starts[0];
    const ECS::Components::Vector2D home{Core::Fixed32(sx), Core::Fixed32(sy)};

    // The nearest tree from twelve tiles out, with room beside it for a camp.
    const int camp_id = cfg.GetUnitIdByName("SMIL");
    entt::entity tree = entt::null, camp = entt::null;
    Core::Fixed32 nearest{};
    auto trees = reg.view<const ECS::Components::Resource, const ECS::Components::Transform>();
    for (auto t : trees) {
        if (trees.get<const ECS::Components::Resource>(t).type != Core::ResourceType::Wood) continue;
        const auto& p = trees.get<const ECS::Components::Transform>(t).position;
        const Core::Fixed32 d = Core::FixedMath::Distance(p.x - home.x, p.y - home.y);
        if (d < Core::Fixed32(12) || (tree != entt::null && d >= nearest)) continue;
        bool room = false;
        for (int dy = -4; dy <= 4 && !room; ++dy) {
            for (int dx = -4; dx <= 4 && !room; ++dx) {
                room = ECS::Buildings::CanPlace(grid, camp_id, p.x.ToInt() + dx, p.y.ToInt() + dy);
            }
        }
        if (!room) continue;
        tree = t;
        nearest = d;
    }
    entt::entity villager = entt::null;
    if (tree == entt::null) {
        verdict = "no tree with room for a camp";
    } else {
        const auto& p = reg.get<ECS::Components::Transform>(tree).position;
        for (int dy = -4; dy <= 4 && camp == entt::null; ++dy) {
            for (int dx = -4; dx <= 4 && camp == entt::null; ++dx) {
                if (ECS::Buildings::CanPlace(grid, camp_id, p.x.ToInt() + dx, p.y.ToInt() + dy)) {
                    camp = ECS::Buildings::Place(reg, grid, camp_id, 1, p.x.ToInt() + dx, p.y.ToInt() + dy, false);
                }
            }
        }
        villager = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), 1,
                                             {p.x + Core::Fixed32(1), p.y}, {}, 0);
        if (camp == entt::null || villager == entt::null) verdict = "the camp or the villager could not be placed";
    }
    bool to_camp = false;
    int delivered = 0;
    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Orders::Execute(reg, grid, Core::GatherCommand{villager, tree, 1});
        for (int t = 0; t < 120 * kTicksPerSecond && delivered == 0 && !to_camp; ++t) {
            sim.Update(16);
            const auto& worker = reg.get<ECS::Components::Worker>(villager);
            to_camp = worker.target_dropoff == camp;
            delivered = worker.delivered;
        }
        if (to_camp) {
            verdict = "a villager took its load to a foundation";
        } else if (delivered == 0) {
            verdict = "the villager never handed its wood in";
        }
    }
    std::printf("unfinisheddrop=%s delivered=%d\n", verdict, delivered);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
