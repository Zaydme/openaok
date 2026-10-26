#include "ecs/Population.h"

#include <algorithm>

#include "ecs/Players.h"
#include "ecs/components/Owner.h"
#include "ecs/components/ProductionQueue.h"

namespace ECS::Population {

namespace R = Core::Rules;

int CostOf(const R::UnitRules& unit) {
    int cost = 0;
    for (int i = 0; i < unit.cost_count; ++i) {
        if (!unit.costs[i].paid &&
            unit.costs[i].resource == R::Res::PopulationHeadroom) {
            cost += unit.costs[i].amount;
        }
    }
    return cost;
}

namespace {

// Population booked by units in training at the player's buildings.
int InTraining(const entt::registry& registry, Core::PlayerId player) {
    int booked = 0;
    auto producing = registry.view<const Components::Owner,
                                   const Components::ProductionQueue>();
    for (auto entity : producing) {
        if (producing.get<const Components::Owner>(entity).player_id != player) continue;
        const auto& queue = producing.get<const Components::ProductionQueue>(entity);
        if (!queue.training || queue.count <= 0 || queue.items[0].tech) continue;
        if (const R::UnitRules* unit =
                Players::UnitOf(registry, player, queue.items[0].id)) {
            booked += CostOf(*unit);
        }
    }
    return booked;
}

} // namespace

int Used(const entt::registry& registry, Core::PlayerId player) {
    const Players::Player* p = Players::Find(registry, player);
    if (p == nullptr) return 0;
    return Players::Resource(*p, R::Res::CurrentPopulation).ToInt() +
           InTraining(registry, player);
}

int Capacity(const entt::registry& registry, Core::PlayerId player) {
    const Players::Player* p = Players::Find(registry, player);
    if (p == nullptr) return 0;
    const int supported =
        Players::Resource(*p, R::Res::CurrentPopulation).ToInt() +
        Players::Resource(*p, R::Res::PopulationHeadroom).ToInt();
    return std::clamp(supported, 0, Players::PopulationLimit(registry));
}

bool HasRoom(const entt::registry& registry, Core::PlayerId player, int cost) {
    return Used(registry, player) + cost <= Capacity(registry, player);
}

} // namespace ECS::Population
