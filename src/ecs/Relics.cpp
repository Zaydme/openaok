#include "ecs/Relics.h"
#include "ecs/Navigation.h"

#include <vector>

#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Relic.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "pathfinding/ApproachPlanner.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Relics {

namespace {

namespace R = Core::Rules;

constexpr int kPickup = 132;          // the dat's "pick up unit" task
constexpr int kDeposit = 136;         // and "deposit unit"
constexpr int kRelicBearerClass = 43; // monk with relic
constexpr int kRelicsCaptured = 7;    // the civ table's count
// Half a gold a second for each relic held.
const Core::Fixed32 kGoldPerRelic = Core::Fixed32(1) / Core::Fixed32(2);
// Close enough to take a relic, or to hand one in at a wall.
const Core::Fixed32 kReach = Core::Fixed32(8) / Core::Fixed32(10);
constexpr int kMaxAttempts = 8;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

const R::UnitRules* RulesOf(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<Components::Identity>(e);
    if (identity == nullptr) return nullptr;
    return Players::RulesFor(registry, e, identity->unit_id);
}

int UnitOf(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<Components::Identity>(e);
    return identity != nullptr ? identity->unit_id : -1;
}

bool HasTask(const R::UnitRules& rules, int action, int target_unit) {
    for (int t = 0; t < rules.task_count; ++t) {
        if (rules.tasks[t].action == action && rules.tasks[t].target_unit == target_unit) {
            return true;
        }
    }
    return false;
}

// The unit a fetcher becomes while it carries a relic: the dat's monk with a
// relic -- the first unit of that class whose tasks deposit one.
int BearerUnit() {
    const auto& units = R::Get().units;
    for (int id = 0; id < static_cast<int>(units.size()); ++id) {
        const R::UnitRules& u = units[id];
        if (!u.valid || u.cls != kRelicBearerClass) continue;
        for (int t = 0; t < u.task_count; ++t) {
            if (u.tasks[t].action == kDeposit) return id;
        }
    }
    return -1;
}

Buildings::Bounds GroundOf(const entt::registry& registry, entt::entity building) {
    return Buildings::FootprintBounds(registry.get<Components::Transform>(building).position,
                                      UnitOf(registry, building));
}

bool RouteTo(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
             entt::entity target) {
    const auto& at = registry.get<Components::Transform>(unit).position;
    Components::Vector2D goal = registry.get<Components::Transform>(target).position;
    if (registry.all_of<Components::Building>(target)) {
        Pathfinding::ApproachRequest ask;
        ask.target = GroundOf(registry, target);
        ask.from = at;
        ask.standoff = Core::Fixed32(3) / Core::Fixed32(10);
        ask.layer = Navigation::LayerOf(registry, grid, unit);
        const auto plan = Pathfinding::FindStandingSpot(grid, ask);
        if (!plan.found) return false;
        goal = plan.spot;
    }
    auto path = Navigation::Route(registry, grid, unit, at, goal);
    if (path.waypoints.empty()) return false;
    registry.emplace_or_replace<Components::Path>(unit, std::move(path));
    return true;
}

Core::Fixed32 GapTo(const entt::registry& registry, entt::entity unit, entt::entity target) {
    const auto& at = registry.get<Components::Transform>(unit).position;
    if (registry.all_of<Components::Building>(target)) return GroundOf(registry, target).DistanceFrom(at);
    const auto& p = registry.get<Components::Transform>(target).position;
    return Core::FixedMath::Distance(p.x - at.x, p.y - at.y);
}

// Puts a relic back on the ground at `at`.
void Drop(entt::registry& registry, entt::entity relic, const Components::Vector2D& at) {
    if (!registry.valid(relic)) return;
    registry.emplace_or_replace<Components::Transform>(relic, Components::Transform{at, at});
    registry.get_or_emplace<Components::Owner>(relic).player_id = Core::PLAYER_GAIA;
}

void PickUp(entt::registry& registry, entt::entity unit, entt::entity relic) {
    const int bearer = BearerUnit();
    if (bearer < 0) return;
    registry.remove<Components::Transform>(relic);
    if (auto* selectable = registry.try_get<Components::Selectable>(relic)) {
        selectable->is_selected = false;
    }
    registry.emplace_or_replace<Components::RelicBearer>(
        unit, Components::RelicBearer{relic, UnitOf(registry, unit)});
    Units::Become(registry, unit, bearer);
}

void HandIn(entt::registry& registry, entt::entity unit, entt::entity monastery) {
    const auto bearer = registry.get<Components::RelicBearer>(unit);
    auto& shrine = registry.get_or_emplace<Components::RelicShrine>(monastery);
    shrine.relics.push_back(bearer.relic);
    if (registry.valid(bearer.relic)) {
        registry.get_or_emplace<Components::Owner>(bearer.relic).player_id =
            OwnerOf(registry, monastery);
    }
    registry.remove<Components::RelicBearer>(unit);
    Units::Become(registry, unit, bearer.was_unit);
    if (Players::Player* player = Players::Find(registry, OwnerOf(registry, monastery))) {
        Players::AddResource(*player, kRelicsCaptured, Core::Fixed32(1));
    }
}

} // namespace

bool CanFetch(const entt::registry& registry, entt::entity unit, entt::entity relic) {
    if (!registry.valid(relic) || !registry.all_of<Components::Transform>(relic)) return false;
    if (registry.any_of<Components::RelicBearer>(unit) || !Combat::Alive(registry, unit)) return false;
    const R::UnitRules* rules = RulesOf(registry, unit);
    return rules != nullptr && HasTask(*rules, kPickup, UnitOf(registry, relic));
}

bool CanDeposit(const entt::registry& registry, entt::entity unit, entt::entity monastery) {
    if (!registry.all_of<Components::RelicBearer>(unit) || !Combat::Alive(registry, monastery)) {
        return false;
    }
    if (OwnerOf(registry, monastery) != OwnerOf(registry, unit)) return false;
    const auto* site = registry.try_get<Components::Building>(monastery);
    if (site == nullptr || site->state != Components::BuildingState::Complete) return false;
    const R::UnitRules* rules = RulesOf(registry, unit);
    return rules != nullptr && HasTask(*rules, kDeposit, UnitOf(registry, monastery));
}

bool Fetch(entt::registry& registry, entt::entity unit, entt::entity relic) {
    if (!CanFetch(registry, unit, relic)) return false;
    registry.remove<Components::Path>(unit);
    registry.emplace_or_replace<Components::RelicErrand>(unit, Components::RelicErrand{relic});
    return true;
}

bool Deposit(entt::registry& registry, entt::entity bearer, entt::entity monastery) {
    if (!CanDeposit(registry, bearer, monastery)) return false;
    registry.remove<Components::Path>(bearer);
    Components::RelicErrand errand;
    errand.monastery = monastery;
    registry.emplace_or_replace<Components::RelicErrand>(bearer, errand);
    return true;
}

int Held(const entt::registry& registry, entt::entity building) {
    const auto* shrine = registry.try_get<Components::RelicShrine>(building);
    return shrine != nullptr ? static_cast<int>(shrine->relics.size()) : 0;
}

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    // Errands.
    std::vector<entt::entity> done;
    std::vector<std::pair<entt::entity, entt::entity>> pickups;
    std::vector<std::pair<entt::entity, entt::entity>> handins;
    auto errands = registry.view<Components::RelicErrand, Components::Transform>();
    for (auto unit : errands) {
        auto& errand = errands.get<Components::RelicErrand>(unit);
        const bool fetching = errand.relic != entt::null;
        const entt::entity target = fetching ? errand.relic : errand.monastery;
        const bool possible = fetching ? CanFetch(registry, unit, target)
                                       : CanDeposit(registry, unit, target);
        if (!possible) {
            done.push_back(unit);
            continue;
        }
        if (GapTo(registry, unit, target) <= kReach) {
            (fetching ? pickups : handins).emplace_back(unit, target);
            continue;
        }
        const auto* path = registry.try_get<Components::Path>(unit);
        if (path != nullptr && path->current_waypoint_index < path->waypoints.size()) continue;
        if (errand.attempts++ >= kMaxAttempts || !RouteTo(registry, grid, unit, target)) {
            done.push_back(unit);
        }
    }
    for (auto unit : done) registry.remove<Components::RelicErrand>(unit);
    for (const auto& [unit, relic] : pickups) {
        registry.remove<Components::RelicErrand>(unit);
        registry.remove<Components::Path>(unit);
        PickUp(registry, unit, relic);
    }
    for (const auto& [unit, monastery] : handins) {
        registry.remove<Components::RelicErrand>(unit);
        registry.remove<Components::Path>(unit);
        HandIn(registry, unit, monastery);
    }

    // A carrier that has fallen drops what it carried.
    std::vector<entt::entity> fallen;
    auto bearers = registry.view<Components::RelicBearer, Components::Transform>();
    for (auto unit : bearers) {
        if (!Combat::Alive(registry, unit)) fallen.push_back(unit);
    }
    for (auto unit : fallen) {
        const auto bearer = registry.get<Components::RelicBearer>(unit);
        Drop(registry, bearer.relic, registry.get<Components::Transform>(unit).position);
        registry.remove<Components::RelicBearer>(unit);
    }

    // Shrines pay.
    auto shrines = registry.view<Components::RelicShrine, Components::Transform>();
    for (auto building : shrines) {
        auto& shrine = shrines.get<Components::RelicShrine>(building);
        Players::Player* player = Players::Find(registry, OwnerOf(registry, building));
        if (player == nullptr || shrine.relics.empty()) continue;
        shrine.gold = shrine.gold + kGoldPerRelic * Core::Fixed32(static_cast<int>(shrine.relics.size())) *
                                        Core::Fixed32(dt_ms) / Core::Fixed32(1000);
        const int whole = shrine.gold.ToInt();
        if (whole > 0) {
            shrine.gold = shrine.gold - Core::Fixed32(whole);
            Players::AddStock(*player, Core::ResourceType::Gold, whole);
            // Relic gold, and collected gold (ecs/Score).
            Score::RelicGold(*player, Core::Fixed32(whole));
        }
    }
}

void Release(entt::registry& registry, entt::entity building) {
    if (!registry.valid(building) || !registry.all_of<Components::RelicShrine>(building)) return;
    {
        auto& shrine = registry.get<Components::RelicShrine>(building);
        const auto ground = GroundOf(registry, building);
        Components::Vector2D at{ground.max_x + Core::Fixed32(1) / Core::Fixed32(2),
                                ground.max_y + Core::Fixed32(1) / Core::Fixed32(2)};
        for (entt::entity relic : shrine.relics) {
            Drop(registry, relic, at);
            at.x = at.x - Core::Fixed32(1);
        }
        if (Players::Player* player = Players::Find(registry, OwnerOf(registry, building))) {
            Players::AddResource(*player, kRelicsCaptured,
                                 Core::Fixed32(-static_cast<int>(shrine.relics.size())));
        }
    }
    registry.remove<Components::RelicShrine>(building);
}

} // namespace ECS::Relics
