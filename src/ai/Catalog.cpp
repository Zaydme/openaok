#include "ai/Catalog.h"

#include <algorithm>

#include "core/Rules.h"
#include "ecs/DropSites.h"
#include "ecs/Players.h"
#include "ecs/VillagerJobs.h"

namespace AI::Catalog {

namespace {

namespace R = Core::Rules;

constexpr int kBuildingType = 80;
constexpr int kFarmClass = 49;

int UnitCount() { return static_cast<int>(R::Get().units.size()); }

const R::UnitRules* RulesOf(const entt::registry& registry, Core::PlayerId player, int id) {
    return ECS::Players::UnitOf(registry, player, id);
}

int Builder(const entt::registry& registry, Core::PlayerId player) {
    (void)registry;
    (void)player;
    return ECS::Villagers::UnitId(ECS::Components::VillagerJob::Build, false);
}

bool GivesHeadroom(const R::UnitRules& rules) {
    for (int s = 0; s < rules.storage_count; ++s) {
        const R::Storage& store = rules.storage[s];
        if (store.resource == R::Res::PopulationHeadroom && store.amount.value > 0 &&
            store.mode == R::StorageMode::WhileStanding) {
            return true;
        }
    }
    return false;
}

// Whether it goes wherever a villager can: its terrain restriction passes
// every ground the villager's does (a galley's does not).
bool GoesOverLand(const R::UnitRules& rules) {
    const R::UnitRules* villager = R::Unit(ECS::Villagers::UnitId(ECS::Components::VillagerJob::Base, false));
    if (villager == nullptr) return true;
    const auto& table = R::Get().terrain_restrictions;
    const size_t terrains = table.empty() ? 0 : table.front().size();
    for (size_t t = 0; t < terrains; ++t) {
        const int dat = static_cast<int>(t);
        if (R::Passable(villager->terrain_restriction, dat) &&
            !R::Passable(rules.terrain_restriction, dat)) {
            return false;
        }
    }
    return true;
}

// Whether it fights on land: an attack of its own, trained rather than built,
// neither a villager nor a missile, and not a ship.
bool Fights(const R::UnitRules& rules) {
    return rules.enabled && rules.type != kBuildingType && rules.attack_count > 0 &&
           rules.reload.value > 0 && rules.speed.value > 0 && rules.train_loc >= 0 &&
           rules.cls != 4 && GoesOverLand(rules);
}

int Strength(const R::UnitRules& rules) {
    int attack = 0;
    for (int a = 0; a < rules.attack_count; ++a) attack = std::max(attack, int(rules.attacks[a].amount));
    return rules.hp * std::max(1, attack);
}

} // namespace

int Villager(const entt::registry& registry, Core::PlayerId player) {
    (void)registry;
    (void)player;
    return ECS::Villagers::UnitId(ECS::Components::VillagerJob::Base, false);
}

bool Buildable(const entt::registry& registry, Core::PlayerId player, int unit_id) {
    const R::UnitRules* rules = RulesOf(registry, player, unit_id);
    return rules != nullptr && rules->enabled && rules->type == kBuildingType &&
           rules->train_loc == Builder(registry, player);
}

int TotalCost(const entt::registry& registry, Core::PlayerId player, int unit_id) {
    const R::UnitRules* rules = RulesOf(registry, player, unit_id);
    if (rules == nullptr) return 0;
    int total = 0;
    for (int c = 0; c < rules->cost_count; ++c) {
        if (rules->costs[c].paid && rules->costs[c].resource <= R::Res::Gold) total += rules->costs[c].amount;
    }
    return total;
}

int House(const entt::registry& registry, Core::PlayerId player) {
    int best = -1;
    int best_cost = 1 << 30;
    for (int id = 0; id < UnitCount(); ++id) {
        if (!Buildable(registry, player, id)) continue;
        const R::UnitRules* rules = RulesOf(registry, player, id);
        if (!GivesHeadroom(*rules)) continue;
        const int cost = TotalCost(registry, player, id);
        if (cost < best_cost) {
            best = id;
            best_cost = cost;
        }
    }
    return best;
}

int DropSite(const entt::registry& registry, Core::PlayerId player, Core::ResourceType resource) {
    const int town = RulesOf(registry, player, Villager(registry, player)) != nullptr
        ? RulesOf(registry, player, Villager(registry, player))->train_loc : -1;
    int best = -1;
    int best_cost = 1 << 30;
    for (int id = 0; id < UnitCount(); ++id) {
        if (id == town || !Buildable(registry, player, id)) continue;
        if (!ECS::DropSites::Accepts(id, resource)) continue;
        const int cost = TotalCost(registry, player, id);
        if (cost < best_cost) {
            best = id;
            best_cost = cost;
        }
    }
    return best;
}

int Farm(const entt::registry& registry, Core::PlayerId player) {
    for (int id = 0; id < UnitCount(); ++id) {
        if (!Buildable(registry, player, id)) continue;
        if (RulesOf(registry, player, id)->cls == kFarmClass) return id;
    }
    return -1;
}

std::vector<int> MilitaryBuildings(const entt::registry& registry, Core::PlayerId player) {
    std::vector<int> out;
    for (int id = 0; id < UnitCount(); ++id) {
        const R::UnitRules* rules = RulesOf(registry, player, id);
        if (rules == nullptr || !Fights(*rules)) continue;
        if (!Buildable(registry, player, rules->train_loc)) continue;
        if (std::find(out.begin(), out.end(), rules->train_loc) == out.end()) out.push_back(rules->train_loc);
    }
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return TotalCost(registry, player, a) < TotalCost(registry, player, b);
    });
    return out;
}

bool Costs(const entt::registry& registry, Core::PlayerId player, int unit_id,
           Core::ResourceType resource) {
    const R::UnitRules* rules = RulesOf(registry, player, unit_id);
    if (rules == nullptr) return false;
    for (int c = 0; c < rules->cost_count; ++c) {
        if (rules->costs[c].paid && R::StockpileOf(rules->costs[c].resource) == resource &&
            rules->costs[c].amount > 0) {
            return true;
        }
    }
    return false;
}

int BestUnitAt(const entt::registry& registry, Core::PlayerId player, int building_id,
               bool affordable) {
    const ECS::Players::Player* seat = ECS::Players::Find(registry, player);
    int best = -1;
    int best_strength = -1;
    for (int id = 0; id < UnitCount(); ++id) {
        const R::UnitRules* rules = RulesOf(registry, player, id);
        if (rules == nullptr || !Fights(*rules) || rules->train_loc != building_id) continue;
        if (rules->button < 1) continue;   // not on the building's panel: a hero, a unique unit elsewhere
        if (affordable && (seat == nullptr || !ECS::Players::CanPay(*seat, rules->costs, rules->cost_count))) {
            continue;
        }
        const int strength = Strength(*rules);
        if (strength > best_strength) {
            best = id;
            best_strength = strength;
        }
    }
    return best;
}

} // namespace AI::Catalog
