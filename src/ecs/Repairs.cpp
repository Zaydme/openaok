#include "ecs/Repairs.h"

#include <algorithm>

#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace ECS::Repairs {

namespace {

namespace R = Core::Rules;

// A ship or a siege weapon mends at a quarter of a building's pace.
const Core::Fixed32 kUnitShare = Core::Fixed32(1) / Core::Fixed32(4);

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// The side's test the repair task's target diplomacy (4) makes: one's own, an
// ally's, or Gaia's.
bool OwnSide(const entt::registry& registry, Core::PlayerId worker, Core::PlayerId target) {
    if (worker == target || target == Core::PLAYER_GAIA) return true;
    const Players::Player* a = Players::Find(registry, worker);
    const Players::Player* b = Players::Find(registry, target);
    return a != nullptr && b != nullptr && a->team != 0 && a->team == b->team;
}

const R::UnitRules* RepairerRules(const entt::registry& registry, Core::PlayerId player) {
    return Players::UnitOf(registry, player,
                           Villagers::UnitId(Components::VillagerJob::Repair, false));
}

} // namespace

bool CanRepair(const entt::registry& registry, entt::entity worker, entt::entity target) {
    if (!registry.valid(worker) || !registry.valid(target) || worker == target) return false;
    const auto* w = registry.try_get<Components::Worker>(worker);
    // A villager: something that works as itself -- a fishing ship -- has no
    // repair task.
    if (w == nullptr || w->gatherer >= 0) return false;
    if (!Combat::Alive(registry, target)) return false;
    const auto* health = registry.try_get<Components::Health>(target);
    if (health == nullptr || health->current_hp >= health->max_hp) return false;
    // A site still going up is built, not mended.
    if (const auto* site = registry.try_get<Components::Building>(target);
        site != nullptr && site->state != Components::BuildingState::Complete) {
        return false;
    }
    if (!OwnSide(registry, OwnerOf(registry, worker), OwnerOf(registry, target))) return false;
    const auto* identity = registry.try_get<Components::Identity>(target);
    const R::UnitRules* what = identity != nullptr ? R::Unit(identity->unit_id) : nullptr;
    const R::UnitRules* repairer = RepairerRules(registry, OwnerOf(registry, worker));
    if (what == nullptr || repairer == nullptr) return false;
    for (int t = 0; t < repairer->task_count; ++t) {
        const R::Task& task = repairer->tasks[t];
        if (task.action != R::TaskAction::Repair) continue;
        // Naming no class and no unit: any building-type object.
        if (task.target_class < 0 && task.target_unit < 0) {
            if (what->type == R::UnitType::Building) return true;
            continue;
        }
        if (task.Targets(identity->unit_id, what->cls)) return true;
    }
    return false;
}

Core::Fixed32 CrewRate(const entt::registry& registry, Core::PlayerId player,
                       entt::entity target, int crew) {
    const R::UnitRules* repairer = RepairerRules(registry, player);
    if (repairer == nullptr || crew <= 0) return Core::Fixed32(0);
    // The first villager at the full rate, each after it at half.
    Core::Fixed32 rate = repairer->work_rate * Core::Fixed32(crew + 1) / Core::Fixed32(2);
    if (!registry.all_of<Components::Building>(target)) rate = rate * kUnitShare;
    return rate;
}

Price PerHitPoint(const entt::registry& registry, entt::entity target) {
    Price price;
    const auto* identity = registry.try_get<Components::Identity>(target);
    const auto* health = registry.try_get<Components::Health>(target);
    if (identity == nullptr || health == nullptr || health->max_hp <= 0) return price;
    const R::UnitRules* rules =
        Players::UnitOf(registry, Players::RulesPlayer(registry, target), identity->unit_id);
    if (rules == nullptr) return price;
    for (int i = 0; i < rules->cost_count; ++i) {
        const R::Cost& cost = rules->costs[i];
        if (!cost.paid || cost.amount <= 0 || cost.resource < 0 || cost.resource > 3) continue;
        // Half the build cost, spread over the hit points.
        price.amount[cost.resource] =
            Core::Fixed32(cost.amount) / Core::Fixed32(2 * health->max_hp);
    }
    return price;
}

int Mend(entt::registry& registry, entt::entity target, int hit_points) {
    auto* health = registry.try_get<Components::Health>(target);
    if (health == nullptr || hit_points <= 0) return 0;
    Players::Player* owner = Players::Find(registry, OwnerOf(registry, target));
    const Price price = PerHitPoint(registry, target);
    int mended = 0;
    while (mended < hit_points && health->current_hp < health->max_hp) {
        if (owner != nullptr) {
            bool can = true;
            for (int r = 0; r < 4; ++r) {
                if (price.amount[r].value > 0 && Players::Resource(*owner, r) < price.amount[r]) {
                    can = false;
                }
            }
            if (!can) break;
            for (int r = 0; r < 4; ++r) {
                if (price.amount[r].value > 0) {
                    Players::AddResource(*owner, r, Core::Fixed32(0) - price.amount[r]);
                }
            }
        }
        ++health->current_hp;
        ++mended;
    }
    if (health->current_hp >= health->max_hp) {
        health->wound = Core::Fixed32(0);
        const auto* at = registry.try_get<Components::Transform>(target);
        Events::Emit(registry, {.kind = Events::Kind::Repaired, .subject = target,
                                .player = OwnerOf(registry, target),
                                .at = at != nullptr ? at->position : Components::Vector2D{}});
    }
    return mended;
}

} // namespace ECS::Repairs
