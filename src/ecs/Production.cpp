#include "ecs/Production.h"

#include "ecs/Cheats.h"
#include "ecs/Events.h"

#include <algorithm>
#include <vector>

#include "core/Random.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Navigation.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Techs.h"
#include "ecs/Units.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/Transform.h"

namespace ECS::Production {

namespace {

namespace R = Core::Rules;

// Which villager actually comes out of the door. The dat trains "the villager"
// as the male base unit; each one comes out male or female at random, and the
// female is a separate unit with its own graphics. The roll comes from the
// simulation's seeded generator, so every machine in a lockstep game rolls the
// same.
int WhoEmerges(entt::registry& registry, Core::PlayerId player, int unit_id) {
    const R::UnitRules* rules = Players::UnitOf(registry, player, unit_id);
    if (rules == nullptr || rules->cls != Units::kVillagerClass) return unit_id;
    if (!registry.ctx().contains<Core::Random>()) return unit_id;
    const bool female = registry.ctx().get<Core::Random>().NextInt(2) == 1;
    if (!female) return unit_id;
    const int female_id = Villagers::UnitId(Components::VillagerJob::Base, true);
    return female_id >= 0 ? female_id : unit_id;
}

// The checks every order shares: the building exists, is the player's and is
// finished. Returns None and the building's unit id when they pass.
Refusal CheckBuilding(const entt::registry& registry, entt::entity building,
                      Core::PlayerId player, int& here) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::Building, Components::Identity,
                         Components::Owner>(building)) {
        return Refusal::NoSuchBuilding;
    }
    if (registry.get<Components::Owner>(building).player_id != player) {
        return Refusal::NotOwned;
    }
    if (registry.get<Components::Building>(building).state !=
        Components::BuildingState::Complete) {
        return Refusal::NotFinished;
    }
    here = registry.get<Components::Identity>(building).unit_id;
    return Refusal::None;
}

// The price of a queue item, for the player.
void PriceOf(const entt::registry& registry, Core::PlayerId player,
             const Components::QueueItem& item, const R::Cost*& costs, int& count) {
    costs = nullptr;
    count = 0;
    const Players::Player* p = Players::Find(registry, player);
    if (item.tech) {
        if (p != nullptr && item.id >= 0 && item.id < static_cast<int>(p->techs.size())) {
            costs = p->techs[item.id].costs;
            count = p->techs[item.id].cost_count;
        }
        return;
    }
    if (const R::UnitRules* rules = Players::UnitOf(registry, player, item.id)) {
        costs = rules->costs;
        count = rules->cost_count;
    }
}

void Refund(entt::registry& registry, Core::PlayerId player,
            const Components::QueueItem& item) {
    const R::Cost* costs = nullptr;
    int count = 0;
    PriceOf(registry, player, item, costs, count);
    Players::Player* p = Players::Find(registry, player);
    if (p != nullptr && costs != nullptr) Players::Pay(*p, costs, count, +1);
    if (item.tech && p != nullptr && item.id >= 0 &&
        item.id < static_cast<int>(p->techs.size()) &&
        p->techs[item.id].state == Players::TechState::Researching) {
        p->techs[item.id].state = Players::TechState::Locked;
    }
}

void PopFront(Components::ProductionQueue& queue) {
    for (int i = 0; i + 1 < queue.count; ++i) queue.items[i] = queue.items[i + 1];
    --queue.count;
    queue.progress_ms = 0;
    queue.training = false;
    queue.housed = false;
}

int32_t DurationMs(const entt::registry& registry, Core::PlayerId player,
                   const Components::QueueItem& item) {
    if (item.tech) {
        const Players::Player* p = Players::Find(registry, player);
        if (p == nullptr || item.id < 0 || item.id >= static_cast<int>(p->techs.size())) {
            return 0;
        }
        return p->techs[item.id].time * 1000;
    }
    const R::UnitRules* rules = Players::UnitOf(registry, player, item.id);
    return rules != nullptr ? rules->train_time * 1000 : 0;
}

} // namespace

Refusal Enqueue(entt::registry& registry, entt::entity building, int unit_id,
                Core::PlayerId player) {
    int here = -1;
    if (const Refusal r = CheckBuilding(registry, building, player, here);
        r != Refusal::None) {
        return r;
    }
    // Only what the player's rules say this building trains: the unit's own
    // train location has to be this building, and the player must have it.
    const R::UnitRules* rules = Players::UnitOf(registry, player, unit_id);
    if (rules == nullptr || rules->train_loc != here) return Refusal::NotProducedHere;
    if (!rules->enabled) return Refusal::Unavailable;

    auto& queue = registry.get_or_emplace<Components::ProductionQueue>(building);
    if (queue.count >= Components::ProductionQueue::kCapacity) return Refusal::QueueFull;
    Players::Player* p = Players::Find(registry, player);
    if (p == nullptr || !Players::CanPay(*p, rules->costs, rules->cost_count)) {
        return Refusal::CannotAfford;
    }

    Players::Pay(*p, rules->costs, rules->cost_count, -1);
    queue.items[queue.count++] = Components::QueueItem{unit_id, false};
    return Refusal::None;
}

Refusal EnqueueTech(entt::registry& registry, entt::entity building, int tech_id,
                    Core::PlayerId player) {
    int here = -1;
    if (const Refusal r = CheckBuilding(registry, building, player, here);
        r != Refusal::None) {
        return r;
    }
    const R::TechRules* tech = R::Tech(tech_id);
    if (tech == nullptr || tech->location != here) return Refusal::NotProducedHere;
    if (!Techs::Available(registry, player, tech_id)) return Refusal::Unavailable;

    auto& queue = registry.get_or_emplace<Components::ProductionQueue>(building);
    if (queue.count >= Components::ProductionQueue::kCapacity) return Refusal::QueueFull;
    Players::Player* p = Players::Find(registry, player);
    if (p == nullptr) return Refusal::CannotAfford;
    Players::TechStatus& status = p->techs[tech_id];
    if (!Players::CanPay(*p, status.costs, status.cost_count)) return Refusal::CannotAfford;

    Players::Pay(*p, status.costs, status.cost_count, -1);
    status.state = Players::TechState::Researching;
    queue.items[queue.count++] = Components::QueueItem{tech_id, true};
    return Refusal::None;
}

bool Cancel(entt::registry& registry, entt::entity building, int slot,
            Core::PlayerId player) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::ProductionQueue, Components::Owner>(building)) {
        return false;
    }
    if (registry.get<Components::Owner>(building).player_id != player) return false;

    auto& queue = registry.get<Components::ProductionQueue>(building);
    if (slot < 0 || slot >= queue.count) return false;

    Refund(registry, player, queue.items[slot]);
    for (int i = slot; i + 1 < queue.count; ++i) queue.items[i] = queue.items[i + 1];
    --queue.count;

    // Taking out the item in progress starts the next one from nothing, and
    // releases the population a unit had booked.
    if (slot == 0) {
        queue.progress_ms = 0;
        queue.training = false;
        queue.housed = false;
    }
    return true;
}

void Abandon(entt::registry& registry, entt::entity building) {
    if (!registry.valid(building) ||
        !registry.all_of<Components::ProductionQueue, Components::Owner>(building)) {
        return;
    }
    const Core::PlayerId owner = registry.get<Components::Owner>(building).player_id;
    auto& queue = registry.get<Components::ProductionQueue>(building);
    while (queue.count > 0) Cancel(registry, building, queue.count - 1, owner);
}

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    // Units that come out of a door, and technologies that complete, change the
    // world -- a unit is created, an upgrade rebuilds a building's art -- which
    // would disturb a view being walked; both are collected and done after.
    struct Arrival {
        int unit_id;
        Core::PlayerId player;
        Components::Vector2D at;
        bool rallied;
        Components::RallyPoint rally;
    };
    struct Discovery {
        int tech_id;
        Core::PlayerId player;
    };
    std::vector<Arrival> arrivals;
    std::vector<Discovery> discoveries;

    // Each player's population, taken once at the start of the pass and then
    // booked against as units start. A unit that finishes this pass is only
    // created once the pass is over, so its slot stays booked here meanwhile.
    int used[Core::kMaxPlayers];
    int capacity[Core::kMaxPlayers];
    bool counted[Core::kMaxPlayers] = {};
    auto population_of = [&](Core::PlayerId player) -> int {
        const int p = static_cast<int>(player) % Core::kMaxPlayers;
        if (!counted[p]) {
            used[p] = Population::Used(registry, player);
            capacity[p] = Population::Capacity(registry, player);
            counted[p] = true;
        }
        return p;
    };

    auto producers = registry.view<Components::ProductionQueue, Components::Building,
                                   Components::Identity, Components::Owner,
                                   Components::Transform>();
    // The players some of whose buildings already wait for room. The first
    // to wait tells the player; the rest say nothing until all have gone on.
    bool waiting[Core::kMaxPlayers] = {};
    for (auto building : producers) {
        if (producers.get<Components::ProductionQueue>(building).housed) {
            waiting[producers.get<Components::Owner>(building).player_id % Core::kMaxPlayers] = true;
        }
    }
    for (auto building : producers) {
        auto& queue = producers.get<Components::ProductionQueue>(building);
        if (queue.count <= 0) continue;
        if (producers.get<Components::Building>(building).state !=
            Components::BuildingState::Complete) {
            continue;
        }
        const Core::PlayerId owner = producers.get<Components::Owner>(building).player_id;
        const Components::QueueItem front = queue.items[0];

        // "aegis": whatever a building makes is done at once.
        const bool instant = Cheats::Aegis(registry, owner);
        if (front.tech) {
            queue.training = true;
            const int32_t needed_ms = instant ? 0 : DurationMs(registry, owner, front);
            queue.progress_ms = std::min(queue.progress_ms + dt_ms, needed_ms);
            if (queue.progress_ms < needed_ms) continue;
            discoveries.push_back({front.id, owner});
            PopFront(queue);
            continue;
        }

        const R::UnitRules* rules = Players::UnitOf(registry, owner, front.id);
        if (rules == nullptr) {
            // A unit missing from the data cannot be made; drop it so it does
            // not block the queue.
            Cancel(registry, building, 0, owner);
            continue;
        }

        // Starting needs a free population slot, and takes it.
        if (!queue.training) {
            const int p = population_of(owner);
            const int cost = Population::CostOf(*rules);
            if (used[p] + cost > capacity[p]) {
                if (!queue.housed && !waiting[p]) {
                    waiting[p] = true;
                    const bool at_limit = capacity[p] >= Players::PopulationLimit(registry);
                    Events::Emit(registry, {.kind = Events::Kind::Halted, .subject = building,
                                            .player = owner, .id = front.id,
                                            .at = producers.get<Components::Transform>(building).position,
                                            .amount = at_limit ? 1 : 0});
                }
                queue.housed = true;
                continue;
            }
            used[p] += cost;
            queue.training = true;
            queue.housed = false;
        }

        const int32_t needed_ms = instant ? 0 : rules->train_time * 1000;
        queue.progress_ms = std::min(queue.progress_ms + dt_ms, needed_ms);
        if (queue.progress_ms < needed_ms) continue;

        // Done: it steps out beside the building. If every bit of wall is
        // blocked it waits, finished, until there is somewhere to stand.
        Components::Vector2D doorstep{};
        if (!Buildings::Doorstep(registry, grid, building, doorstep,
                                 Navigation::LayerOfUnit(registry, grid, owner, front.id))) {
            continue;
        }

        const auto* rally = registry.try_get<Components::RallyPoint>(building);
        arrivals.push_back({front.id, owner, doorstep, rally != nullptr,
                            rally != nullptr ? *rally : Components::RallyPoint{}});
        PopFront(queue);
    }

    for (const Arrival& arrival : arrivals) {
        const entt::entity unit = Units::Spawn(
            registry, WhoEmerges(registry, arrival.player, arrival.unit_id),
            arrival.player, arrival.at);
        if (unit != entt::null) {
            Events::Emit(registry, {.kind = Events::Kind::Born, .subject = unit,
                                    .player = arrival.player,
                                    .id = registry.get<Components::Identity>(unit).unit_id,
                                    .at = arrival.at});
        }
        // Out of the door and off to the building's rally point.
        if (arrival.rallied && unit != entt::null) {
            Orders::FollowRally(registry, grid, unit, arrival.rally.at, arrival.rally.target);
        }
    }
    for (const Discovery& discovery : discoveries) {
        Techs::Complete(registry, discovery.player, discovery.tech_id);
    }
}

int32_t FrontDuration(const entt::registry& registry, entt::entity building) {
    const auto* queue = registry.try_get<Components::ProductionQueue>(building);
    const auto* owner = registry.try_get<Components::Owner>(building);
    if (queue == nullptr || owner == nullptr || queue->count <= 0) return 0;
    return DurationMs(registry, owner->player_id, queue->items[0]);
}

} // namespace ECS::Production
