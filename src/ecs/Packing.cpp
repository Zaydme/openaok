#include "ecs/Packing.h"

#include <algorithm>
#include <vector>

#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Events.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Packing.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "pathfinding/ApproachPlanner.h"

namespace ECS::Packing {

namespace {

namespace R = Core::Rules;

// Its task that sends it at an enemy to set up and shoot.
constexpr int kUnpackAndAttack = 125;
// How often a packed engine on its way re-plans, and how many empty routes
// before it gives the target up -- as a chase does (ecs/Combat).
constexpr int32_t kReplanMs = 1000;
constexpr int kGiveUp = 3;

const R::UnitRules* RulesOf(const entt::registry& registry, entt::entity e) {
    const auto* identity = registry.try_get<Components::Identity>(e);
    return identity != nullptr ? Players::RulesFor(registry, e, identity->unit_id) : nullptr;
}

// Armed when its rules can fight: a Combat component, as Units::Spawn gives
// one; disarmed otherwise.
void ArmFor(entt::registry& registry, entt::entity unit) {
    const R::UnitRules* rules = RulesOf(registry, unit);
    const bool fights = rules != nullptr && rules->attack_count > 0 && rules->reload.value > 0;
    if (fights) {
        if (!registry.all_of<Components::Combat>(unit)) registry.emplace<Components::Combat>(unit);
    } else {
        registry.remove<Components::Combat>(unit);
    }
}

void Finish(entt::registry& registry, entt::entity unit, const Components::Transforming& done) {
    Units::Become(registry, unit, done.to_unit);
    ArmFor(registry, unit);
    registry.remove<Components::Path>(unit);
    const auto* at = registry.try_get<Components::Transform>(unit);
    const auto* owner = registry.try_get<Components::Owner>(unit);
    Events::Emit(registry, {.kind = done.packing ? Events::Kind::Packed : Events::Kind::Unpacked,
                            .subject = unit,
                            .player = owner != nullptr ? owner->player_id : Core::PLAYER_GAIA,
                            .id = done.to_unit,
                            .at = at != nullptr ? at->position : Components::Vector2D{}});
    if (!done.packing && Combat::Alive(registry, done.then_attack)) {
        Combat::Engage(registry, unit, done.then_attack, true);
    }
}

// The range it will shoot from once set up: the unpacked unit's.
const R::UnitRules* SetUpRules(const entt::registry& registry, entt::entity unit) {
    const R::UnitRules* rules = RulesOf(registry, unit);
    if (rules == nullptr || rules->transform_unit < 0) return nullptr;
    return Players::RulesFor(registry, unit, rules->transform_unit);
}

bool PlanApproach(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
                  entt::entity target, Core::Fixed32 range) {
    const auto& from = registry.get<Components::Transform>(unit).position;
    const auto& at = registry.get<Components::Transform>(target).position;
    Pathfinding::ApproachRequest ask;
    const auto* aimed = registry.try_get<Components::Identity>(target);
    ask.target = registry.all_of<Components::Building>(target) && aimed != nullptr
               ? Buildings::FootprintBounds(at, aimed->unit_id) : Buildings::TileBounds(at);
    ask.from = from;
    // Comfortably inside its range, so the last step's drift leaves it there.
    ask.standoff = range - Core::Fixed32(1);
    ask.clearance = Core::Fixed32(1) / Core::Fixed32(2);
    ask.layer = Navigation::LayerOf(registry, grid, unit);
    const auto spot = Pathfinding::FindStandingSpot(grid, ask);
    auto path = Navigation::Route(registry, grid, unit, from, spot.found ? spot.spot : at);
    if (path.waypoints.empty()) return false;
    registry.emplace_or_replace<Components::Path>(unit, std::move(path));
    return true;
}

} // namespace

bool Packs(const entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || registry.all_of<Components::Building>(entity)) return false;
    const R::UnitRules* rules = RulesOf(registry, entity);
    return rules != nullptr && rules->transform_unit >= 0;
}

bool Packed(const entt::registry& registry, entt::entity entity) {
    const R::UnitRules* rules = RulesOf(registry, entity);
    return rules != nullptr && rules->cls == R::UnitClass::PackedUnit;
}

bool Busy(const entt::registry& registry, entt::entity unit) {
    return registry.valid(unit) && registry.all_of<Components::Transforming>(unit);
}

int ProgressPercent(const entt::registry& registry, entt::entity unit) {
    const auto* t = registry.try_get<Components::Transforming>(unit);
    if (t == nullptr || t->total_ms <= 0) return 0;
    return 100 - t->left_ms * 100 / t->total_ms;
}

bool Start(entt::registry& registry, entt::entity unit, bool pack) {
    if (!Packs(registry, unit) || Busy(registry, unit)) return false;
    if (!registry.all_of<Components::Transform>(unit)) return false;
    if (Packed(registry, unit) == pack) return false;
    const R::UnitRules* rules = RulesOf(registry, unit);
    // Whatever it was doing stops: it neither walks nor shoots while at it.
    registry.remove<Components::Path, Components::Assaulting>(unit);
    registry.remove<Components::Combat>(unit);
    registry.emplace<Components::Transforming>(
        unit, Components::Transforming{.to_unit = rules->transform_unit, .left_ms = kPackMs,
                                       .total_ms = kPackMs, .packing = pack});
    return true;
}

bool Assault(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
             entt::entity target) {
    if (!Packs(registry, unit) || !Packed(registry, unit) || Busy(registry, unit)) return false;
    const R::UnitRules* rules = RulesOf(registry, unit);
    bool sent = false;
    for (int t = 0; t < rules->task_count; ++t) {
        if (rules->tasks[t].action == kUnpackAndAttack) sent = true;
    }
    if (!sent || !Combat::Alive(registry, target) || !Combat::Hostile(registry, unit, target)) {
        return false;
    }
    (void)grid;
    registry.remove<Components::Path>(unit);
    registry.emplace_or_replace<Components::Assaulting>(unit, Components::Assaulting{target, 0, 0});
    return true;
}

void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms) {
    // Those setting up or packing: done when their time is up. Collected
    // first, since becoming another unit changes components a live view sees.
    std::vector<std::pair<entt::entity, Components::Transforming>> done;
    auto busy = registry.view<Components::Transforming>();
    for (auto unit : busy) {
        auto& t = busy.get<Components::Transforming>(unit);
        if (!Combat::Alive(registry, unit)) continue;
        t.left_ms -= dt_ms;
        if (t.left_ms <= 0) done.emplace_back(unit, t);
    }
    std::sort(done.begin(), done.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [unit, t] : done) {
        registry.remove<Components::Transforming>(unit);
        Finish(registry, unit, t);
    }

    // Those on their way to set up: in range, they unpack; otherwise on.
    std::vector<std::pair<entt::entity, entt::entity>> arrived;
    std::vector<entt::entity> dropped;
    auto going = registry.view<Components::Assaulting, Components::Transform>();
    for (auto unit : going) {
        auto& a = going.get<Components::Assaulting>(unit);
        if (!Combat::Alive(registry, unit)) continue;
        if (!Combat::Alive(registry, a.target)) {
            dropped.push_back(unit);
            continue;
        }
        const R::UnitRules* set_up = SetUpRules(registry, unit);
        if (set_up == nullptr) {
            dropped.push_back(unit);
            continue;
        }
        const Core::Fixed32 gap = Combat::Gap(registry, unit, a.target);
        if (gap <= set_up->max_range && gap >= set_up->min_range) {
            arrived.emplace_back(unit, a.target);
            continue;
        }
        a.replan_ms -= dt_ms;
        const auto* path = registry.try_get<Components::Path>(unit);
        const bool walking = path != nullptr && path->current_waypoint_index < path->waypoints.size();
        if (a.replan_ms <= 0 || !walking) {
            a.replan_ms = kReplanMs;
            if (PlanApproach(registry, grid, unit, a.target, set_up->max_range)) {
                a.failures = 0;
            } else if (++a.failures >= kGiveUp) {
                dropped.push_back(unit);
            }
        }
    }
    for (entt::entity unit : dropped) registry.remove<Components::Assaulting>(unit);
    for (const auto& [unit, target] : arrived) {
        registry.remove<Components::Assaulting>(unit);
        registry.remove<Components::Path>(unit);
        if (Start(registry, unit, false)) {
            registry.get<Components::Transforming>(unit).then_attack = target;
        }
    }
}

void Cancel(entt::registry& registry, entt::entity unit) {
    if (!Busy(registry, unit)) {
        registry.remove<Components::Assaulting>(unit);
        return;
    }
    registry.remove<Components::Transforming>(unit);
    ArmFor(registry, unit);
}

} // namespace ECS::Packing
