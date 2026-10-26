#include "ai/Census.h"

#include <algorithm>

#include "core/Rules.h"
#include "ecs/Animals.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Techs.h"
#include "ecs/VillagerJobs.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Duty.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/ProductionQueue.h"
#include "ecs/components/Worker.h"

namespace AI {

namespace {

namespace R = Core::Rules;

constexpr int kFarmClass = 49;
constexpr int kTradeCartClass = 19;
constexpr int kTradeBoatClass = 2;
constexpr int kTransportClass = 20;
constexpr int kWarBoatClass = 22;

} // namespace

int Census::Count(int unit_id, bool finished_only) const {
    int n = 0;
    for (const Site& site : buildings) {
        if (site.unit_id == unit_id && (site.complete || !finished_only)) ++n;
    }
    return n;
}

entt::entity Census::Any(int unit_id) const {
    for (const Site& site : buildings) {
        if (site.unit_id == unit_id && site.complete) return site.entity;
    }
    return entt::null;
}

int Census::BuildersOn(const entt::registry& registry, entt::entity site) const {
    int n = 0;
    for (entt::entity v : villagers) {
        const auto& worker = registry.get<ECS::Components::Worker>(v);
        if (worker.state == Core::UnitState::Building && worker.target_building == site) ++n;
    }
    return n;
}

Census Take(const entt::registry& registry, Core::PlayerId player) {
    Census census;
    census.player = player;
    census.age = ECS::Techs::Age(registry, player);
    if (const ECS::Players::Player* seat = ECS::Players::Find(registry, player)) {
        for (int r = 0; r < 4; ++r) {
            census.stock[r] = ECS::Players::Stock(*seat, static_cast<Core::ResourceType>(r));
        }
    }
    census.population = ECS::Population::Used(registry, player);
    census.capacity = ECS::Population::Capacity(registry, player);
    census.population_limit = ECS::Players::PopulationLimit(registry);

    // Buildings, and which one trains villagers.
    const auto* villager = ECS::Players::UnitOf(
        registry, player, ECS::Villagers::UnitId(ECS::Components::VillagerJob::Base, false));
    auto buildings = registry.view<const ECS::Components::Building, const ECS::Components::Identity,
                                   const ECS::Components::Owner, const ECS::Components::Transform>();
    std::vector<entt::entity> farms;
    for (auto b : buildings) {
        if (buildings.get<const ECS::Components::Owner>(b).player_id != player) continue;
        if (!ECS::Combat::Alive(registry, b)) continue;
        Census::Site site;
        site.entity = b;
        site.unit_id = buildings.get<const ECS::Components::Identity>(b).unit_id;
        site.complete = buildings.get<const ECS::Components::Building>(b).state ==
                        ECS::Components::BuildingState::Complete;
        const auto* queue = registry.try_get<ECS::Components::ProductionQueue>(b);
        site.idle = queue == nullptr || queue->count == 0;
        census.buildings.push_back(site);
        if (site.complete && census.town_center == entt::null && villager != nullptr &&
            villager->train_loc == site.unit_id) {
            census.town_center = b;
            const auto ground = ECS::Buildings::FootprintBounds(
                buildings.get<const ECS::Components::Transform>(b).position, site.unit_id);
            census.home = {(ground.min_x + ground.max_x) / Core::Fixed32(2),
                           (ground.min_y + ground.max_y) / Core::Fixed32(2)};
        }
        const R::UnitRules* rules = ECS::Players::UnitOf(registry, player, site.unit_id);
        if (site.complete && rules != nullptr && rules->cls == kFarmClass) farms.push_back(b);
    }

    // Villagers and soldiers, out in the world.
    auto units = registry.view<const ECS::Components::Identity, const ECS::Components::Owner,
                               const ECS::Components::Transform>();
    for (auto u : units) {
        if (units.get<const ECS::Components::Owner>(u).player_id != player) continue;
        if (registry.all_of<ECS::Components::Building>(u) || !ECS::Combat::Alive(registry, u)) continue;
        const auto* path = registry.try_get<ECS::Components::Path>(u);
        const bool walking = path != nullptr && path->current_waypoint_index < path->waypoints.size();
        if (const auto* worker = registry.try_get<ECS::Components::Worker>(u)) {
            if (worker->gatherer >= 0) {
                census.fishing_ships.push_back(u);
                if (worker->state == Core::UnitState::Idle && !walking) {
                    census.idle_fishing_ships.push_back(u);
                }
                continue;
            }
            census.villagers.push_back(u);
            if (registry.any_of<ECS::Components::Boarding>(u)) continue;
            switch (worker->state) {
            case Core::UnitState::Idle:
                if (!walking) census.idle_villagers.push_back(u);
                break;
            case Core::UnitState::Building:
                ++census.builders;
                break;
            default:
                if (registry.valid(worker->target_resource) || worker->state != Core::UnitState::Idle) {
                    ++census.gatherers[static_cast<int>(worker->carry_type)];
                }
                break;
            }
            for (size_t f = 0; f < farms.size(); ++f) {
                if (worker->target_resource == farms[f]) {
                    farms[f] = farms.back();
                    farms.pop_back();
                    break;
                }
            }
            continue;
        }
        if (ECS::Animals::Herdable(registry, u)) {
            census.herd.push_back(u);
            continue;
        }
        const int id = units.get<const ECS::Components::Identity>(u).unit_id;
        const auto* rules = ECS::Players::UnitOf(registry, player, id);
        const int cls = rules != nullptr ? rules->cls : -1;
        if (cls == kTradeCartClass || cls == kTradeBoatClass) {
            census.trade.push_back(u);
            continue;
        }
        if (cls == kTransportClass) {
            census.transports.push_back(u);
            continue;
        }
        const auto* combat = registry.try_get<ECS::Components::Combat>(u);
        if (combat == nullptr && !registry.all_of<ECS::Components::Monk>(u)) continue;
        census.military.push_back(u);
        if (cls == kWarBoatClass) census.boats.push_back(u);
        else census.soldiers.push_back(u);
        const bool idle = (combat == nullptr || combat->state == ECS::Components::CombatState::Idle) &&
                          !walking && !registry.any_of<ECS::Components::Duty>(u);
        if (idle) census.idle_military.push_back(u);
        // The fast, far-sighted horseman the game starts every player with is
        // the scout; the rest are the army.
        if (census.scout == entt::null && cls != kWarBoatClass && rules != nullptr &&
            rules->los >= Core::Fixed32(4) && rules->speed >= Core::Fixed32(1)) {
            census.scout = u;
        }
    }
    census.idle_farms = static_cast<int>(farms.size());
    // The scout scouts; it is not part of the army.
    if (census.scout != entt::null) {
        std::erase(census.military, census.scout);
        std::erase(census.idle_military, census.scout);
    }
    return census;
}

void Prune(const entt::registry& registry, Census& census) {
    auto stands = [&](entt::entity e) {
        return registry.valid(e) && registry.all_of<ECS::Components::Transform>(e) &&
               ECS::Combat::Alive(registry, e);
    };
    for (std::vector<entt::entity>* list :
         {&census.villagers, &census.idle_villagers, &census.fishing_ships, &census.idle_fishing_ships,
          &census.trade, &census.military, &census.idle_military, &census.soldiers, &census.boats,
          &census.transports, &census.herd}) {
        std::erase_if(*list, [&](entt::entity e) { return !stands(e); });
    }
    // A foundation laid this pass has no entity yet; one that had and is
    // gone, is gone.
    std::erase_if(census.buildings, [&](const Census::Site& site) {
        return site.entity != entt::null && !stands(site.entity);
    });
    if (census.town_center != entt::null && !stands(census.town_center)) census.town_center = entt::null;
    if (census.scout != entt::null && !stands(census.scout)) census.scout = entt::null;
}

} // namespace AI
