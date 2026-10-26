#include "ecs/Duties.h"
#include "ecs/Navigation.h"

#include <vector>

#include "core/FixedMath.h"
#include "ecs/Combat.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Duty.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Duties {

namespace {

// A patrol has reached an end once this close to it.
const Core::Fixed32 kArrived = Core::Fixed32(1) / Core::Fixed32(2);
// How near a guard or a follower keeps to its charge, and how far it lets it
// get before closing up again.
const Core::Fixed32 kKeepBy = Core::Fixed32(3) / Core::Fixed32(2);
const Core::Fixed32 kCloseUp = Core::Fixed32(5) / Core::Fixed32(2);
// How often a guard or follower re-plans after a charge on the move.
constexpr int32_t kReplanMs = 500;
// How far a guard looks for whoever is attacking its charge.
const Core::Fixed32 kGuardWatch = Core::Fixed32(6);

bool Walking(const entt::registry& registry, entt::entity unit) {
    const auto* path = registry.try_get<Components::Path>(unit);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size();
}

bool Fighting(const entt::registry& registry, entt::entity unit) {
    const auto* combat = registry.try_get<Components::Combat>(unit);
    return combat != nullptr && combat->state != Components::CombatState::Idle;
}

void WalkTo(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
            const Components::Vector2D& to) {
    const auto& at = registry.get<Components::Transform>(unit).position;
    auto path = Navigation::Route(registry, grid, unit, at, to);
    if (!path.waypoints.empty()) registry.emplace_or_replace<Components::Path>(unit, std::move(path));
}

void Begin(entt::registry& registry, entt::entity unit, Components::Duty duty) {
    if (!registry.all_of<Components::Transform>(unit)) return;
    registry.remove<Components::Path>(unit);
    registry.emplace_or_replace<Components::Duty>(unit, duty);
}

// Whoever is attacking `charge`, nearest to the guard first.
entt::entity Assailant(const entt::registry& registry, entt::entity guard, entt::entity charge) {
    const auto& at = registry.get<Components::Transform>(guard).position;
    entt::entity best = entt::null;
    Core::Fixed32 best_d{};
    auto fighters = registry.view<const Components::Combat, const Components::Transform>();
    for (auto e : fighters) {
        if (fighters.get<const Components::Combat>(e).target != charge) continue;
        if (!Combat::Alive(registry, e) || !Combat::Hostile(registry, guard, e)) continue;
        const auto& p = fighters.get<const Components::Transform>(e).position;
        const Core::Fixed32 d = Core::FixedMath::Distance(p.x - at.x, p.y - at.y);
        if (d > kGuardWatch) continue;
        if (best == entt::null || d < best_d || (d == best_d && e < best)) {
            best = e;
            best_d = d;
        }
    }
    return best;
}

} // namespace

void Patrol(entt::registry& registry, entt::entity unit, const Components::Vector2D& to) {
    if (!registry.all_of<Components::Transform>(unit)) return;
    Components::Duty duty;
    duty.kind = Components::DutyKind::Patrol;
    duty.from = registry.get<Components::Transform>(unit).position;
    duty.to = to;
    Begin(registry, unit, duty);
}

void Guard(entt::registry& registry, entt::entity unit, entt::entity charge) {
    if (unit == charge || !Combat::Alive(registry, charge)) return;
    Components::Duty duty;
    duty.kind = Components::DutyKind::Guard;
    duty.charge = charge;
    Begin(registry, unit, duty);
}

void Follow(entt::registry& registry, entt::entity unit, entt::entity charge) {
    if (unit == charge || !Combat::Alive(registry, charge)) return;
    Components::Duty duty;
    duty.kind = Components::DutyKind::Follow;
    duty.charge = charge;
    Begin(registry, unit, duty);
}

void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms) {
    std::vector<entt::entity> ended;
    std::vector<std::pair<entt::entity, Components::Vector2D>> walks;
    std::vector<std::pair<entt::entity, entt::entity>> fights;

    auto view = registry.view<Components::Duty, Components::Transform>();
    for (auto unit : view) {
        auto& duty = view.get<Components::Duty>(unit);
        if (!Combat::Alive(registry, unit)) {
            ended.push_back(unit);
            continue;
        }
        // A fight in progress is the unit's own business; the duty resumes
        // when it is over.
        if (Fighting(registry, unit)) continue;
        const auto& at = view.get<Components::Transform>(unit).position;

        switch (duty.kind) {
        case Components::DutyKind::Patrol: {
            if (Walking(registry, unit)) break;
            const Components::Vector2D& end = duty.outbound ? duty.to : duty.from;
            if (Core::FixedMath::Distance(at.x - end.x, at.y - end.y) <= kArrived) {
                duty.outbound = !duty.outbound;
            }
            walks.emplace_back(unit, duty.outbound ? duty.to : duty.from);
            break;
        }
        case Components::DutyKind::Guard:
        case Components::DutyKind::Follow: {
            if (!Combat::Alive(registry, duty.charge) ||
                !registry.all_of<Components::Transform>(duty.charge)) {
                ended.push_back(unit);
                break;
            }
            if (duty.kind == Components::DutyKind::Guard) {
                const entt::entity foe = Assailant(registry, unit, duty.charge);
                if (foe != entt::null && registry.all_of<Components::Combat>(unit)) {
                    fights.emplace_back(unit, foe);
                    break;
                }
            }
            duty.replan_ms -= dt_ms;
            const auto& there = registry.get<Components::Transform>(duty.charge).position;
            const Core::Fixed32 gap = Core::FixedMath::Distance(at.x - there.x, at.y - there.y);
            if (gap <= kCloseUp && !Walking(registry, unit)) break;
            if (gap <= kKeepBy) {
                registry.remove<Components::Path>(unit);
                break;
            }
            if (duty.replan_ms > 0 && Walking(registry, unit)) break;
            duty.replan_ms = kReplanMs;
            // A spot short of the charge, on the near side.
            const Core::Fixed32 back = kKeepBy / gap;
            walks.emplace_back(unit, Components::Vector2D{there.x + (at.x - there.x) * back,
                                                          there.y + (at.y - there.y) * back});
            break;
        }
        }
    }
    for (entt::entity unit : ended) registry.remove<Components::Duty>(unit);
    for (const auto& [unit, to] : walks) WalkTo(registry, grid, unit, to);
    // Defending the charge is the guard order itself, so the fight is pursued
    // as an ordered one whatever the guard's stance; once the assailant is
    // dead the duty takes the guard back to its charge.
    for (const auto& [unit, foe] : fights) Combat::Engage(registry, unit, foe, true);
}

} // namespace ECS::Duties
