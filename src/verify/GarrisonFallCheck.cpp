#include "verify/Harness.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Garrisons.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Transform.h"

namespace Verify {

// A Watch Tower of ours with a villager inside, every other tile of the map
// blocked, destroyed: the villager stands on the ground the tower held, not
// lost inside it.
bool CheckGarrisonFall() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const int tower_id = cfg.GetUnitIdByName("WCTW");
    entt::entity tower = entt::null;
    int tx = 0, ty = 0;
    for (int y = grid.GetHeight() / 2; y < grid.GetHeight() - 20 && tower == entt::null; ++y) {
        for (int x = grid.GetWidth() / 2; x < grid.GetWidth() - 20 && tower == entt::null; ++x) {
            if (ECS::Buildings::CanPlace(grid, tower_id, x, y)) {
                tower = ECS::Buildings::Place(reg, grid, tower_id, 1, x, y, true);
                tx = x;
                ty = y;
            }
        }
    }
    entt::entity villager = entt::null;
    if (tower == entt::null) {
        verdict = "no ground for a watch tower";
    } else {
        const ECS::Components::Vector2D near{Core::Fixed32(tx), Core::Fixed32(ty)};
        villager = ECS::Units::Spawn(reg, cfg.GetUnitIdByName("VMBAS"), 1, near);
        ECS::Garrisons::PutInside(reg, villager, tower);
        if (!reg.all_of<ECS::Components::Garrisoned>(villager)) verdict = "the villager did not go in";
    }
    // The tiles the tower stands on.
    std::vector<std::pair<int, int>> held;
    if (tower != entt::null) {
        ECS::Buildings::ForEachFootprintTile(tower_id, tx, ty, [&](int x, int y) { held.emplace_back(x, y); });
    }
    auto on_held = [&](int x, int y) { return std::find(held.begin(), held.end(), std::make_pair(x, y)) != held.end(); };
    if (std::strcmp(verdict, "ok") == 0) {
        for (int y = 0; y < grid.GetHeight(); ++y) {
            for (int x = 0; x < grid.GetWidth(); ++x) {
                if (!on_held(x, y)) grid.SetWalkable(x, y, false);
            }
        }
        ECS::Buildings::Destroy(reg, grid, tower);
        if (!reg.valid(villager) || reg.any_of<ECS::Components::Garrisoned>(villager) ||
            !reg.all_of<ECS::Components::Transform>(villager)) {
            verdict = "a garrison with nowhere to step out was lost with its building";
        } else {
            const auto& at = reg.get<ECS::Components::Transform>(villager).position;
            if (!on_held(Physics::TileOf(at.x), Physics::TileOf(at.y))) {
                verdict = "a garrison with nowhere to step out stood off the building's ground";
            }
        }
    }
    std::printf("garrisonfall=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
