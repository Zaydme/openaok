#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// A farmer at a finished Farm of its player's for forty seconds: every tick
// it works, it stands on the field, and it works at several spots of it in
// turn, still gathering as it steps from one to the next.
bool CheckFarmField() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const int farm_id = cfg.GetUnitIdByName("FARM");
    const auto [sx, sy] = sim.player_starts[0];
    entt::entity farm = entt::null;
    int fx = 0, fy = 0;
    for (int r = 5; r < 20 && farm == entt::null; ++r) {
        for (int d = -r; d <= r && farm == entt::null; ++d) {
            if (ECS::Buildings::CanPlace(grid, farm_id, sx + d, sy + r)) {
                fx = sx + d;
                fy = sy + r;
                farm = ECS::Buildings::Place(reg, grid, farm_id, 1, fx, fy, true);
            }
        }
    }
    std::vector<std::pair<int, int>> field;
    if (farm != entt::null) {
        ECS::Buildings::ForEachFootprintTile(farm_id, fx, fy, [&](int x, int y) { field.emplace_back(x, y); });
    }
    const entt::entity farmer =
        farm == entt::null ? entt::null
                           : ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), 1,
                                                       {Core::Fixed32(fx - 2), Core::Fixed32(fy - 2)}, {}, 0);
    int worked = 0, off = 0, stepping = 0;
    Core::Fixed32 progress{};
    std::vector<ECS::Components::Vector2D> spots;
    if (farm == entt::null || farmer == entt::null) {
        verdict = "no farm or farmer placed";
    } else {
        ECS::Orders::Execute(reg, grid, Core::GatherCommand{farmer, farm, 1});
        for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
            sim.Update(16);
            const auto& worker = reg.get<ECS::Components::Worker>(farmer);
            const Core::Fixed32 was = progress;
            progress = worker.gather_progress;
            const auto* step = reg.try_get<ECS::Components::Path>(farmer);
            if (worker.state == Core::UnitState::Gathering && !worker.working && step != nullptr &&
                step->current_waypoint_index < step->waypoints.size() && progress > was) {
                ++stepping;
            }
            if (worker.state != Core::UnitState::Gathering || !worker.working) continue;
            ++worked;
            const auto& at = reg.get<ECS::Components::Transform>(farmer).position;
            const std::pair<int, int> tile{Physics::TileOf(at.x), Physics::TileOf(at.y)};
            if (std::find(field.begin(), field.end(), tile) == field.end()) ++off;
            bool known = false;
            for (const auto& s : spots) {
                known = known || Core::FixedMath::Distance(s.x - at.x, s.y - at.y) < Core::Fixed32(1) / Core::Fixed32(4);
            }
            if (!known) spots.push_back(at);
        }
        if (worked == 0) {
            verdict = "the farmer never worked its farm";
        } else if (off > 0) {
            verdict = "a farmer worked from off its field";
        } else if (spots.size() < 3) {
            verdict = "a farmer worked its field from one spot";
        } else if (stepping == 0) {
            verdict = "a farmer stopped gathering while it stepped to its next spot";
        }
    }
    std::printf("farmfield=%s worked=%d off=%d spots=%zu stepping=%d\n", verdict, worked, off, spots.size(), stepping);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
