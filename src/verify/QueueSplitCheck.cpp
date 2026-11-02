#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "ecs/Buildings.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Selection.h"
#include "ecs/Simulation.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Selectable.h"

namespace Verify {

namespace {

int Queued(const entt::registry& registry, entt::entity building) {
    const auto* queue = registry.try_get<ECS::Components::ProductionQueue>(building);
    return queue != nullptr ? queue->count : 0;
}

} // namespace

// Three Barracks selected, one of them still a foundation: the panel
// commands a finished one, and militia ordered one at a time go one to each
// finished Barracks in turn, never to the foundation, until both queues are
// full.
bool CheckQueueSplit() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    constexpr Core::PlayerId kUs = 1;
    const auto [sx, sy] = sim.player_starts[0];
    const int barracks = cfg.GetUnitIdByName("BRKS");
    const int militia = cfg.GetUnitIdByName("SPRMN");
    const entt::entity first = ECS::Buildings::Place(reg, grid, barracks, kUs, sx + 8, sy, true);
    const entt::entity site = ECS::Buildings::Place(reg, grid, barracks, kUs, sx, sy + 8, false);
    const entt::entity second = ECS::Buildings::Place(reg, grid, barracks, kUs, sx - 8, sy, true);
    int counts[3] = {0, 0, 0};
    if (first == entt::null || site == entt::null || second == entt::null) {
        verdict = "no Barracks placed";
    } else {
        if (ECS::Players::Player* us = ECS::Players::Find(reg, kUs)) {
            for (auto type : {Core::ResourceType::Food, Core::ResourceType::Wood, Core::ResourceType::Gold,
                              Core::ResourceType::Stone}) {
                ECS::Players::AddStock(*us, type, 100000);
            }
        }
        auto all = reg.view<ECS::Components::Selectable>();
        for (auto e : all) {
            all.get<ECS::Components::Selectable>(e).is_selected = e == first || e == site || e == second;
        }
        const entt::entity commanded = ECS::Selection::Commanded(reg, kUs);
        std::vector<entt::entity> others;
        ECS::Selection::Alike(reg, kUs, commanded, others);
        if (commanded == site) {
            verdict = "a foundation commands a selection with finished Barracks";
        } else if (others.size() != 2) {
            verdict = "the other selected Barracks are not found";
        }
        const int capacity = ECS::Components::ProductionQueue::kCapacity;
        for (int n = 0; n < 2 * capacity + 3 && verdict == nullptr; ++n) {
            ECS::Orders::Execute(reg, grid, Core::TrainCommand{commanded, militia, kUs, others});
            const int a = Queued(reg, first), b = Queued(reg, second);
            if (Queued(reg, site) != 0) {
                verdict = "a unit went to a foundation";
            } else if (a + b != std::min(n + 1, 2 * capacity) || a - b > 1 || b - a > 1) {
                verdict = "units were not shared one to each Barracks in turn";
            }
        }
        counts[0] = Queued(reg, first);
        counts[1] = Queued(reg, second);
        counts[2] = Queued(reg, site);
        if (verdict == nullptr && (counts[0] != capacity || counts[1] != capacity)) {
            verdict = "the Barracks' queues were not filled";
        }
    }
    std::printf("queuesplit=%s first=%d second=%d site=%d\n", verdict == nullptr ? "ok" : verdict, counts[0],
                counts[1], counts[2]);
    return verdict == nullptr;
}

} // namespace Verify
