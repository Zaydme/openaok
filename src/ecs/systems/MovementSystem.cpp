#include "MovementSystem.h"

#include <algorithm>
#include <cstdlib>

#include "core/FixedMath.h"
#include "ecs/Combat.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Systems {

namespace {

// A unit's speed when it has no rules to read one from: a villager's.
const Core::Fixed32 kFallbackSpeed = Core::Fixed32(8) / Core::Fixed32(10);

// How near a corner a unit looks past it to the one after, to take the
// corner as soon as that one is in sight.
const Core::Fixed32 kLookAhead(1);

// How far inside its range a walk to something ends, so whoever measures the
// range after it finds the walker within it.
const Core::Fixed32 kInside = Core::Fixed32(2) / Core::Fixed32(100);

// How many times a route is planned again on one order for ground that has
// closed across it, before the order is given up.
constexpr int8_t kMostReplans = 4;

// A unit that comes up against someone standing on its goal takes the
// nearest free place round the goal: on square rings this far apart -- the
// spacing units keep standing in a formation -- out to this many, moving on
// at most this many times.
const Core::Fixed32 kRingSpacing = Core::Fixed32(7) / Core::Fixed32(10);
constexpr int kRings = 4;
constexpr int8_t kMostMovesOn = 4;

// How far a unit walks in one tick: its speed in the owner's rules -- the
// dat's tiles per second, raised by technologies such as Wheelbarrow -- over
// the tick's share of a second.
// A tick's step for a unit whose rules are `rules` (null: none found); one
// without an identity or an owner walks at the fallback speed.
Core::Fixed32 StepLength(bool identified, const Core::Rules::UnitRules* rules, int32_t dt_ms) {
    Core::Fixed32 speed = kFallbackSpeed;
    // No speed in its rules, no step: a trebuchet set up stays put.
    if (identified && rules != nullptr) speed = rules->speed.value > 0 ? rules->speed : Core::Fixed32(0);
    return speed * Core::Fixed32(dt_ms) / Core::Fixed32(1000);
}

void FinishOrder(Components::Path& path) {
    path.waypoints.clear();
    path.target = entt::null;
    path.current_waypoint_index = 0;
    path.replans = 0;
    path.has_goal = false;
}

// Plans the way to the goal again from where the unit stands. False when
// there is no way left.
bool Replan(const entt::registry& registry, const Physics::Grid& grid, entt::entity entity,
            Components::Path& path, const Components::Transform& transform) {
    if (!path.has_goal || path.replans >= kMostReplans) return false;
    auto route = Navigation::Route(registry, grid, entity, transform.position, path.goal,
                                   path.target, path.range);
    if (route.waypoints.empty()) return false;
    path.waypoints = std::move(route.waypoints);
    path.current_waypoint_index = 0;
    ++path.replans;
    return true;
}

// Whoever stands on the goal, for a unit coming to it to stop as it comes up
// against: the first body, other than its own, its own body at the goal
// would overlap, the bodies round as the standing spots are laid
// (pathfinding/ApproachPlanner). Null for none.
const Navigation::Body* OnTheGoal(const Navigation::Traffic& traffic, entt::entity self,
                                  Components::Vector2D goal, Core::Fixed32 radius) {
    const int gx = Physics::TileOf(goal.x);
    const int gy = Physics::TileOf(goal.y);
    for (int ty = gy - 1; ty <= gy + 1; ++ty) {
        if (ty < 0 || ty >= traffic.height) continue;
        const int x0 = std::max(0, gx - 1);
        const int x1 = std::min(traffic.width - 1, gx + 1);
        const size_t row = static_cast<size_t>(ty) * traffic.width;
        for (int32_t i = traffic.first[row + x0]; i < traffic.first[row + x1 + 1]; ++i) {
            const Navigation::Body& b = traffic.bodies[static_cast<size_t>(i)];
            if (b.entity == self) continue;
            const Core::Fixed32 reach = b.radius + radius;
            if (Core::FixedMath::Abs(b.x - goal.x) < reach && Core::FixedMath::Abs(b.y - goal.y) < reach &&
                Core::FixedMath::Distance(b.x - goal.x, b.y - goal.y) < reach) {
                return &b;
            }
        }
    }
    return nullptr;
}

// Whether a body of `radius` at `at` would stand over anyone standing still,
// other than `self`.
bool Taken(const Navigation::Traffic& traffic, entt::entity self, Components::Vector2D at, Core::Fixed32 radius) {
    const int tx = Physics::TileOf(at.x);
    const int ty = Physics::TileOf(at.y);
    for (int y = ty - 1; y <= ty + 1; ++y) {
        if (y < 0 || y >= traffic.height) continue;
        const int x0 = std::max(0, tx - 1);
        const int x1 = std::min(traffic.width - 1, tx + 1);
        if (x0 > x1) continue;
        const size_t row = static_cast<size_t>(y) * traffic.width;
        for (int32_t i = traffic.first[row + x0]; i < traffic.first[row + x1 + 1]; ++i) {
            const Navigation::Body& b = traffic.bodies[static_cast<size_t>(i)];
            if (b.entity == self || b.moving) continue;
            const Core::Fixed32 reach = b.radius + radius;
            if (Core::FixedMath::Abs(b.x - at.x) < reach && Core::FixedMath::Abs(b.y - at.y) < reach) return true;
        }
    }
    return false;
}

// How near two bodies may come, in parts of their radii together: a crowd
// overlaps a little, never stands one over another.
const Core::Fixed32 kKeepApart = Core::Fixed32(3) / Core::Fixed32(4);

// Where a step from `from` to `at` ends with the bodies about it kept off:
// pushed out from each it would stand over -- by more than kKeepApart allows
// -- by no more than the step went, and only where the ground lets it go. The
// walker's target is not kept off; it is walked up to.
Components::Vector2D KeptApart(const Navigation::Traffic& traffic, const Physics::Grid& grid,
                               Physics::Layer layer, entt::entity self, entt::entity target,
                               Components::Vector2D from, Components::Vector2D at, Core::Fixed32 radius) {
    if (at.x == from.x && at.y == from.y) return at;
    const int tx = Physics::TileOf(at.x);
    const int ty = Physics::TileOf(at.y);
    Core::Fixed32 push_x{}, push_y{};
    for (int y = ty - 1; y <= ty + 1; ++y) {
        if (y < 0 || y >= traffic.height) continue;
        const int x0 = std::max(0, tx - 1);
        const int x1 = std::min(traffic.width - 1, tx + 1);
        if (x0 > x1) continue;
        const size_t row = static_cast<size_t>(y) * traffic.width;
        for (int32_t i = traffic.first[row + x0]; i < traffic.first[row + x1 + 1]; ++i) {
            const Navigation::Body& b = traffic.bodies[static_cast<size_t>(i)];
            if (b.entity == self || b.entity == target) continue;
            const Core::Fixed32 need = (b.radius + radius) * kKeepApart;
            const Core::Fixed32 dx = at.x - b.x;
            const Core::Fixed32 dy = at.y - b.y;
            if (Core::FixedMath::Abs(dx) >= need || Core::FixedMath::Abs(dy) >= need) continue;
            const Core::Fixed32 d = Core::FixedMath::Distance(dx, dy);
            if (d >= need) continue;
            if (d.value == 0) {
                // Right on top of it: out to the side of the step, the side
                // set by which of the two is the elder.
                const Core::Fixed32 sx = at.x - from.x, sy = at.y - from.y;
                const bool left = self < b.entity;
                const Core::Fixed32 ox = left ? Core::Fixed32(0) - sy : sy;
                const Core::Fixed32 oy = left ? sx : Core::Fixed32(0) - sx;
                if (ox.value == 0 && oy.value == 0) continue;
                const auto out = Core::FixedMath::NormalizeAndScale(ox, oy, need);
                push_x = push_x + out.x;
                push_y = push_y + out.y;
                continue;
            }
            push_x = push_x + dx * (need - d) / d;
            push_y = push_y + dy * (need - d) / d;
        }
    }
    if (push_x.value == 0 && push_y.value == 0) return at;
    const Core::Fixed32 went = Core::FixedMath::Distance(at.x - from.x, at.y - from.y);
    if (went.value == 0) return at;
    // Never back the way it came, which could hold it still against a crowd
    // for good: what would push it back turns it aside instead, round them.
    const Core::Fixed32 ux = (at.x - from.x) / went, uy = (at.y - from.y) / went;
    const Core::Fixed32 back = push_x * ux + push_y * uy;
    if (back.value < 0) {
        push_x = push_x - back * ux;
        push_y = push_y - back * uy;
        const Core::Fixed32 aside = Core::FixedMath::Distance(push_x, push_y);
        const Core::Fixed32 owed = Core::FixedMath::Abs(back) - aside;
        if (owed.value > 0) {
            // Head on: to the side the push leans to, or the walker's left.
            Core::Fixed32 sx = Core::Fixed32(0) - uy, sy = ux;
            if (aside.value > 0) {
                sx = push_x / aside;
                sy = push_y / aside;
            }
            push_x = push_x + sx * owed;
            push_y = push_y + sy * owed;
        }
    }
    const Core::Fixed32 pushed = Core::FixedMath::Distance(push_x, push_y);
    if (pushed > went) {
        push_x = push_x * went / pushed;
        push_y = push_y * went / pushed;
    }
    const Components::Vector2D kept{at.x + push_x, at.y + push_y};
    if (!grid.Passable(layer, Physics::TileOf(kept.x), Physics::TileOf(kept.y)) ||
        !Pathfinding::AStar::MiddleClear(grid, from, kept, layer)) {
        return at;
    }
    return kept;
}

// The free place round `goal` nearest `from`, on the rings about it, that a
// straight walk from `from` reaches; false when there is none.
bool PlaceRound(const entt::registry& registry, const Physics::Grid& grid, const Navigation::Traffic& traffic,
                entt::entity self, Components::Vector2D goal, Components::Vector2D from, Core::Fixed32 radius,
                Components::Vector2D& out) {
    const Physics::Layer layer = Navigation::LayerOf(registry, grid, self);
    for (int ring = 1; ring <= kRings; ++ring) {
        bool found = false;
        Core::Fixed32 best{};
        for (int j = -ring; j <= ring; ++j) {
            for (int k = -ring; k <= ring; ++k) {
                if (std::max(std::abs(j), std::abs(k)) != ring) continue;
                const Components::Vector2D at{goal.x + kRingSpacing * Core::Fixed32(j),
                                              goal.y + kRingSpacing * Core::Fixed32(k)};
                if (!grid.InBounds(Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
                if (!Navigation::CanStand(registry, grid, self, at) || Taken(traffic, self, at, radius)) continue;
                if (!Pathfinding::AStar::MiddleClear(grid, from, at, layer)) continue;
                const Core::Fixed32 apart = Core::FixedMath::Distance(at.x - from.x, at.y - from.y);
                if (!found || apart < best) {
                    found = true;
                    best = apart;
                    out = at;
                }
            }
        }
        if (found) return true;
    }
    return false;
}

} // namespace

// The walk: straight along the route at the unit's speed, landing on each
// corner and going on round it in the same step, and taking a corner early
// once the one after it is in sight. Units pass over one another as they do
// in the original; nobody is pushed or slid. A unit sent to a point another
// already stands on moves on, as it comes up against them, to the free place
// nearest it round the point. Ground that closes across the way has the route
// planned again.
void MovementSystem::Update(entt::registry& registry, const Physics::Grid& grid,
                            int32_t fixed_dt_ms) {
    const Navigation::Traffic* traffic = Navigation::TrafficOf(registry);
    const auto& identities = registry.storage<Components::Identity>();
    const auto& owners = registry.storage<Components::Owner>();
    const auto& makers = registry.storage<Components::RulesOf>();
    const auto& workers = registry.storage<Components::Worker>();
    const auto& bodies = registry.storage<Components::Transform>();
    const Players::Table* table = Players::TableOf(registry);
    auto view = registry.view<Components::Transform, Components::Path, Components::Sprite>();
    for (auto entity : view) {
        auto& transform = view.get<Components::Transform>(entity);
        auto& path = view.get<Components::Path>(entity);
        auto& sprite = view.get<Components::Sprite>(entity);
        if (path.current_waypoint_index >= path.waypoints.size()) continue;

        // Remember where this order was for, so it can be planned again.
        if (!path.has_goal) {
            path.goal = path.waypoints.back();
            path.has_goal = true;
        }
        // Its rules, once: the layer it moves on, its body and its pace, as
        // Navigation::LayerOf, RadiusOf and StepLength find them.
        const auto* identity = identities.contains(entity) ? &identities.get(entity) : nullptr;
        const auto* owner = owners.contains(entity) ? &owners.get(entity) : nullptr;
        const Core::PlayerId rules_player = makers.contains(entity) ? makers.get(entity).player
                                            : owner != nullptr      ? owner->player_id
                                                                    : Core::PLAYER_GAIA;
        const Core::Rules::UnitRules* rules =
            identity != nullptr ? Players::UnitOf(table, rules_player, identity->unit_id) : nullptr;
        const Physics::Layer layer =
            identity == nullptr ? Physics::kLand
                                : Physics::Keyed(rules != nullptr ? grid.LayerOf(rules->terrain_restriction) : Physics::kLand,
                                                 owner != nullptr ? owner->player_id : Core::PLAYER_GAIA);
        const Core::Fixed32 radius = rules != nullptr ? rules->size_x : Core::Fixed32(0);

        // Near a corner with the next one in sight, the corner is cut.
        if (path.current_waypoint_index + 1 < path.waypoints.size()) {
            const auto& corner = path.waypoints[path.current_waypoint_index];
            if (Core::FixedMath::Distance(corner.x - transform.position.x,
                                          corner.y - transform.position.y) <= kLookAhead &&
                Pathfinding::AStar::BodyClear(grid, transform.position,
                                              path.waypoints[path.current_waypoint_index + 1],
                                              layer, radius)) {
                ++path.current_waypoint_index;
            }
        }

        Core::Fixed32 step = StepLength(identity != nullptr && owner != nullptr, rules, fixed_dt_ms);
        if (path.speed_cap.value > 0) {
            step = Core::FixedMath::Min(
                step, path.speed_cap * Core::Fixed32(fixed_dt_ms) / Core::Fixed32(1000));
        }
        if (step.value == 0) continue;

        // A walk to something ends within range of it. One to a point ends
        // there, or, someone already standing on it, up against them.
        const bool aimed = path.target != entt::null && registry.valid(path.target) && bodies.contains(path.target);
        // Within range of its target, as InRange: the target's box grown by
        // its body, worked out once.
        Buildings::Bounds reach{};
        if (aimed) {
            const Buildings::Bounds box = Combat::BoxOf(registry, path.target);
            reach = {box.min_x - radius, box.min_y - radius, box.max_x + radius, box.max_y + radius};
        }
        auto in_range = [&](Components::Vector2D point) {
            const Core::Fixed32 gap = reach.DistanceFrom(point);
            return gap.value == 0 || gap + kInside <= path.range;
        };
        const Navigation::Body* occupant =
            !aimed && traffic != nullptr ? OnTheGoal(*traffic, entity, path.goal, radius) : nullptr;

        Components::Vector2D at = transform.position;
        Components::Vector2D heading{};
        bool arrived = false;
        bool blocked = false;   // up against whoever stands on the goal
        bool closed = false;
        while (step.value > 0 && path.current_waypoint_index < path.waypoints.size()) {
            if (aimed && in_range(at)) {
                arrived = true;
                break;
            }
            if (occupant != nullptr) {
                const Core::Fixed32 reach = occupant->radius + radius;
                if (Core::FixedMath::Abs(occupant->x - at.x) <= reach &&
                    Core::FixedMath::Abs(occupant->y - at.y) <= reach) {
                    arrived = true;
                    blocked = true;
                    break;
                }
            }
            const auto& waypoint = path.waypoints[path.current_waypoint_index];
            const Core::Fixed32 to_x = waypoint.x - at.x;
            const Core::Fixed32 to_y = waypoint.y - at.y;
            const Core::Fixed32 distance = Core::FixedMath::Distance(to_x, to_y);
            Components::Vector2D next = waypoint;
            if (distance > step) {
                next = {at.x + to_x * step / distance, at.y + to_y * step / distance};
            }
            if (!Pathfinding::AStar::MiddleClear(grid, at, next, layer)) {
                closed = true;
                break;
            }
            if (distance.value > 0) heading = {to_x, to_y};
            if (distance > step) {
                at = next;
                step = Core::Fixed32(0);
            } else {
                at = waypoint;
                step = step - distance;
                ++path.current_waypoint_index;
            }
        }
        // Kept off the others about it, but for a step that comes up
        // against whoever stands on its goal (OnTheGoal). A walk to something
        // that is kept out of reach of it walks on.
        if (traffic != nullptr && !blocked) {
            at = KeptApart(*traffic, grid, layer, entity, path.target, transform.position, at, radius);
            if (aimed) arrived = in_range(at);
        }
        transform.position = at;
        if (heading.x.value != 0 || heading.y.value != 0) {
            sprite.heading = Core::FixedMath::WorldVectorToHeading(heading.x, heading.y);
        }
        if (aimed && in_range(at)) {
            arrived = true;
        }
        if (closed && !arrived && !Replan(registry, grid, entity, path, transform)) arrived = true;
        // Another unit stands where it was going: a unit sent there moves on
        // to the free place nearest it round the goal, rather than standing
        // over others. Something that never walks -- a relic -- it comes up
        // to.
        Components::Vector2D place{};
        const auto* occupant_rules = blocked ? Players::RulesFor(registry, occupant->entity,
                                                               registry.get<Components::Identity>(occupant->entity).unit_id)
                                             : nullptr;
        const bool walker = occupant_rules != nullptr && occupant_rules->speed.value > 0;
        // A villager walking to its work keeps to the spot its work gave it.
        const auto* worker = workers.contains(entity) ? &workers.get(entity) : nullptr;
        const bool sent = worker == nullptr || worker->state == Core::UnitState::Idle;
        if (blocked && walker && sent && traffic != nullptr && path.moved_on < kMostMovesOn &&
            PlaceRound(registry, grid, *traffic, entity, path.goal, at, radius, place)) {
            path.waypoints.assign(1, place);
            path.current_waypoint_index = 0;
            path.goal = place;
            ++path.moved_on;
            continue;
        }
        if (arrived || path.current_waypoint_index >= path.waypoints.size()) FinishOrder(path);
    }
}

} // namespace ECS::Systems
