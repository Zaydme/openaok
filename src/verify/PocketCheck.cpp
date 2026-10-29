#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;

int RegionOf(const ECS::Simulation& sim, C::Vector2D at) {
    return Pathfinding::RegionAt(sim.GetGrid(), Physics::TileOf(at.x), Physics::TileOf(at.y), Physics::kLand);
}

} // namespace

// Villagers sent at a deer in a clearing walled in by trees, which no walk
// reaches, give it up and stand, as an order that cannot be carried out
// leaves them, rather than standing set on it for ever.
bool CheckPocket() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    std::vector<entt::entity> villagers;
    for (auto e : reg.view<C::Worker, C::Owner>()) {
        if (reg.get<C::Owner>(e).player_id == 1 && reg.get<C::Worker>(e).gatherer < 0) villagers.push_back(e);
    }
    entt::entity deer = entt::null;
    if (villagers.size() < 2) {
        verdict = "no villagers";
    } else {
        villagers.resize(2);
        const int home = RegionOf(sim, reg.get<C::Transform>(villagers[0]).position);
        for (auto e : reg.view<C::Identity, C::Transform, C::Resource>()) {
            if (reg.get<C::Identity>(e).unit_id != cfg.GetUnitIdByName("DEERX")) continue;
            if (RegionOf(sim, reg.get<C::Transform>(e).position) == home) continue;
            deer = e;
            break;
        }
        if (deer == entt::null) verdict = "no deer out of reach on the map";
    }
    if (verdict == nullptr) {
        for (auto v : villagers) Core::CommandQueue::Get().PushCommand(Core::GatherCommand{v, deer, 1});
        for (int t = 0; t < 40 * kTicksPerSecond; ++t) sim.Update(16);
        for (auto v : villagers) {
            const auto& w = reg.get<C::Worker>(v);
            if (w.state != Core::UnitState::Idle || w.target_resource == deer) {
                verdict = "a villager sent at a deer no walk reaches did not give it up";
            }
        }
    }
    std::printf("pocket=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
