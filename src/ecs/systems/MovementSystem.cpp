#include "MovementSystem.h"

#include "core/FixedMath.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

#include <unordered_map>
#include <vector>

namespace ECS::Systems {

namespace {

// A unit's speed when it has no rules to read one from: a villager's.
const Core::Fixed32 kFallbackSpeed = Core::Fixed32(8) / Core::Fixed32(10);

// How close two units may stand. Separation only acts inside this, so a unit
// walking down an empty lane is never deflected by someone a tile away.
const Core::Fixed32 kSeparation = Core::Fixed32(70) / Core::Fixed32(100);

// How close a unit may get to someone working: closer than usual, so a crew
// stands shoulder to shoulder along a site's wall and the rank behind can
// still reach it to build.
const Core::Fixed32 kWorkingSeparation = Core::Fixed32(50) / Core::Fixed32(100);

// How hard separation pulls, relative to heading for the waypoint. Both are
// unit vectors before they are mixed, so this weight means the same thing
// whether the goal is one tile away or forty.
const Core::Fixed32 kAvoidWeight = Core::Fixed32(150) / Core::Fixed32(100);
// The sideways part, which is what turns "stop" into "go around".
const Core::Fixed32 kSlideWeight = Core::Fixed32(120) / Core::Fixed32(100);

// Idle units unstack, gently: a position nudge, not a steering force.
const Core::Fixed32 kUnstackPush = Core::Fixed32(4) / Core::Fixed32(100);

// A corner is retired early -- it only exists to be rounded -- while the
// destination needs to be reached properly.
const Core::Fixed32 kCornerRadius = Core::Fixed32(45) / Core::Fixed32(100);
const Core::Fixed32 kArriveRadius = Core::Fixed32(12) / Core::Fixed32(100);
// Standing on the exact destination is often impossible because someone is
// already there. Once a unit is this close and getting nowhere, it has
// arrived as far as anyone can tell.
const Core::Fixed32 kCrowdedArrive = Core::Fixed32(160) / Core::Fixed32(100);

// Getting closer to the waypoint by less than this fraction of a unit's own
// step counts as getting nowhere.
const Core::Fixed32 kProgressShare = Core::Fixed32(1) / Core::Fixed32(4);

// Blocked for this long and the route is rebuilt from where the unit actually
// stands; after this many rebuilds the order is abandoned rather than retried
// forever.
constexpr int16_t kTicksBeforeRepath = 24;
constexpr int8_t kMaxRepaths = 4;
constexpr int16_t kTicksBeforeGivingUp = 150;

// One entry per unit that can be walked into.
struct Body {
    entt::entity entity;
    Core::Fixed32 x;
    Core::Fixed32 y;
    bool moving;
    // Swinging a hammer at a building. Others are allowed much closer.
    bool working;
};

// Which bodies sit on which tile, so neighbour lookup stays linear.
struct Neighbourhood {
    std::vector<Body> bodies;
    std::unordered_map<int64_t, std::vector<size_t>> buckets;
    std::unordered_map<uint32_t, size_t> by_entity;

    static int64_t Key(int tx, int ty) {
        return (static_cast<int64_t>(tx) << 32) ^ static_cast<uint32_t>(ty);
    }

    void Build(entt::registry& registry) {
        bodies.clear();
        buckets.clear();
        by_entity.clear();

        // Every unit, not only villagers: a scout or a soldier is walked into
        // just the same. A unit is what has an animation and is not a building.
        auto view = registry.view<Components::Transform, Components::Animation,
                                  Components::Identity>();
        for (auto entity : view) {
            if (registry.all_of<Components::Building>(entity)) continue;
            const auto& transform = view.get<Components::Transform>(entity);
            bool moving = false;
            if (const auto* path = registry.try_get<Components::Path>(entity)) {
                moving = path->current_waypoint_index < path->waypoints.size();
            }
            // Working and fighting both let others in close: a building site
            // is a scrum, and so is a melee -- at the ordinary spacing two
            // swordsmen are held out of each other's reach.
            const auto* worker = registry.try_get<Components::Worker>(entity);
            const auto* combat = registry.try_get<Components::Combat>(entity);
            const bool close_work =
                (worker != nullptr && worker->working) ||
                (combat != nullptr && combat->state == Components::CombatState::Attacking);
            by_entity.emplace(static_cast<uint32_t>(entt::to_integral(entity)),
                              bodies.size());
            bodies.push_back({entity, transform.position.x, transform.position.y,
                              moving, close_work});
        }
        for (size_t i = 0; i < bodies.size(); ++i) {
            buckets[Key(Physics::TileOf(bodies[i].x), Physics::TileOf(bodies[i].y))].push_back(i);
        }
    }

    // Sum of directions away from everyone too close, weighted by how close.
    // `standing_only` restricts it to units that are not going anywhere, which
    // is what an idle unit is allowed to be pushed apart by: a passer-by must
    // never shove someone off the spot they were told to hold.
    void Separation(size_t index, bool standing_only,
                    Core::Fixed32& out_x, Core::Fixed32& out_y) const {
        out_x = Core::Fixed32(0);
        out_y = Core::Fixed32(0);
        const Body& self = bodies[index];
        const int tx = Physics::TileOf(self.x);
        const int ty = Physics::TileOf(self.y);

        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                auto bucket = buckets.find(Key(tx + ox, ty + oy));
                if (bucket == buckets.end()) continue;
                for (size_t other : bucket->second) {
                    if (other == index) continue;
                    if (standing_only && bodies[other].moving) continue;

                    // Someone at work may be crowded right up against.
                    const Core::Fixed32 keep_out =
                        (bodies[other].working || self.working)
                            ? kWorkingSeparation : kSeparation;

                    const Core::Fixed32 dx = self.x - bodies[other].x;
                    const Core::Fixed32 dy = self.y - bodies[other].y;
                    const Core::Fixed32 distance = Core::FixedMath::Distance(dx, dy);
                    if (distance >= keep_out) continue;

                    if (distance.value == 0) {
                        // Exactly coincident: split them by entity order so the
                        // pair separates instead of drifting together forever.
                        const Core::Fixed32 nudge =
                            (index < other) ? Core::Fixed32(1) : Core::Fixed32(-1);
                        out_x = out_x + nudge;
                        continue;
                    }
                    const Core::Fixed32 strength =
                        (keep_out - distance) / keep_out;
                    const auto away =
                        Core::FixedMath::NormalizeAndScale(dx, dy, strength);
                    out_x = out_x + away.x;
                    out_y = out_y + away.y;
                }
            }
        }
    }
};

// Whether a unit may stand on a tile, on its own layer: a ship on the sea, a
// villager on land. Movement checks this as well as the pathfinder, since
// separation can push a unit sideways into a tree, or a ship up the beach.
bool Walkable(const Physics::Grid& grid, Physics::Layer layer, Core::Fixed32 x,
              Core::Fixed32 y) {
    return grid.Passable(layer, Physics::TileOf(x), Physics::TileOf(y));
}

// Moves as far along (vx, vy) as the ground allows: straight if it can, along
// the wall if something is in the way, nowhere if it is boxed in. Returns how
// far it actually got, which is what tells a blocked unit from a walking one.
//
// Sliding keeps the full speed, not only the component along the wall, so a
// unit walking almost straight into a wall does not creep along it slowly
// enough to count as blocked.
Core::Fixed32 StepWithSliding(const Physics::Grid& grid, Physics::Layer layer,
                              Components::Transform& transform,
                              Core::Fixed32 vx, Core::Fixed32 vy) {
    const Core::Fixed32 x = transform.position.x;
    const Core::Fixed32 y = transform.position.y;

    if (Walkable(grid, layer, x + vx, y + vy)) {
        transform.position.x = x + vx;
        transform.position.y = y + vy;
        return Core::FixedMath::Distance(vx, vy);
    }

    const Core::Fixed32 speed = Core::FixedMath::Distance(vx, vy);
    if (speed.value == 0) return Core::Fixed32(0);

    // Try the axis the unit was mostly heading along first, so it rounds a
    // corner the short way.
    const bool x_first = Core::FixedMath::Abs(vx) >= Core::FixedMath::Abs(vy);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool along_x = (attempt == 0) == x_first;
        if (along_x) {
            if (vx.value == 0) continue;
            const Core::Fixed32 step = vx.value > 0 ? speed : Core::Fixed32(0) - speed;
            if (!Walkable(grid, layer, x + step, y)) continue;
            transform.position.x = x + step;
            return speed;
        }
        if (vy.value == 0) continue;
        const Core::Fixed32 step = vy.value > 0 ? speed : Core::Fixed32(0) - speed;
        if (!Walkable(grid, layer, x, y + step)) continue;
        transform.position.y = y + step;
        return speed;
    }
    return Core::Fixed32(0);
}

// Rebuilds the route from where the unit stands to where it was sent. Returns
// false when there is no way through, which is the only case that should ever
// abandon an order.
bool Repath(const Physics::Grid& grid, Physics::Layer layer, Components::Path& path,
            const Components::Transform& transform) {
    if (!path.has_goal) return false;
    // To the goal itself, not to the middle of the tile it is in.
    auto route = Pathfinding::AStar::FindPathTo(grid, transform.position,
                                                path.goal, layer);
    if (route.waypoints.empty()) return false;

    path.waypoints = std::move(route.waypoints);
    path.current_waypoint_index = 0;
    path.closest_for = static_cast<size_t>(-1);
    path.blocked_ticks = 0;
    path.avoid_side = 0;
    ++path.repaths;
    return true;
}

void FinishOrder(Components::Path& path) {
    path.waypoints.clear();
    path.current_waypoint_index = 0;
    path.blocked_ticks = 0;
    path.repaths = 0;
    path.avoid_side = 0;
    path.has_goal = false;
}

// How far a unit walks in one tick: its speed in the owner's rules -- the
// dat's tiles per second, raised by technologies such as Wheelbarrow -- over
// the tick's share of a second.
Core::Fixed32 StepLength(const entt::registry& registry, entt::entity entity,
                         int32_t dt_ms) {
    Core::Fixed32 speed = kFallbackSpeed;
    const auto* identity = registry.try_get<Components::Identity>(entity);
    const auto* owner = registry.try_get<Components::Owner>(entity);
    if (identity != nullptr && owner != nullptr) {
        if (const auto* rules = Players::UnitOf(registry, Players::RulesPlayer(registry, entity),
                                                identity->unit_id)) {
            // No speed in its rules, no step: a trebuchet set up stays put.
            speed = rules->speed.value > 0 ? rules->speed : Core::Fixed32(0);
        }
    }
    return speed * Core::Fixed32(dt_ms) / Core::Fixed32(1000);
}

} // namespace

void MovementSystem::Update(entt::registry& registry, const Physics::Grid& grid,
                            int32_t fixed_dt_ms) {
    static Neighbourhood crowd;
    crowd.Build(registry);

    auto view = registry.view<Components::Transform, Components::Path,
                              Components::Sprite>();
    for (auto entity : view) {
        auto& transform = view.get<Components::Transform>(entity);
        auto& path = view.get<Components::Path>(entity);
        auto& sprite = view.get<Components::Sprite>(entity);

        if (path.current_waypoint_index >= path.waypoints.size()) continue;

        // Remember where this order was for, so it can be rebuilt later.
        if (!path.has_goal) {
            path.goal = path.waypoints.back();
            path.has_goal = true;
        }

        // Retire every waypoint already reached. A corner counts as reached
        // from further away than the destination does: it exists to be rounded.
        while (path.current_waypoint_index < path.waypoints.size()) {
            const auto& waypoint = path.waypoints[path.current_waypoint_index];
            const Core::Fixed32 distance = Core::FixedMath::Distance(
                waypoint.x - transform.position.x,
                waypoint.y - transform.position.y);
            const bool last = path.current_waypoint_index + 1 == path.waypoints.size();
            if (distance > (last ? kArriveRadius : kCornerRadius)) break;
            ++path.current_waypoint_index;
        }
        if (path.current_waypoint_index >= path.waypoints.size()) {
            FinishOrder(path);
            continue;
        }

        const auto& waypoint = path.waypoints[path.current_waypoint_index];
        const Core::Fixed32 to_x = waypoint.x - transform.position.x;
        const Core::Fixed32 to_y = waypoint.y - transform.position.y;

        // Heading and avoidance are both unit vectors before they are mixed, so
        // their relative weight does not change with distance to the goal.
        const auto heading =
            Core::FixedMath::NormalizeAndScale(to_x, to_y, Core::Fixed32(1));
        Core::Fixed32 steer_x = heading.x;
        Core::Fixed32 steer_y = heading.y;

        auto found = crowd.by_entity.find(
            static_cast<uint32_t>(entt::to_integral(entity)));
        Core::Fixed32 push_x(0);
        Core::Fixed32 push_y(0);
        if (found != crowd.by_entity.end()) {
            crowd.Separation(found->second, false, push_x, push_y);
        }

        if (push_x.value != 0 || push_y.value != 0) {
            // A sideways component makes a unit slide around someone it meets
            // head-on rather than stall. The side is committed on first
            // contact and held; the two halves of a pair pick opposite ways,
            // identically on every machine.
            if (path.avoid_side == 0) {
                const Core::Fixed32 cross = heading.x * push_y - heading.y * push_x;
                path.avoid_side = (cross.value >= 0) ? 1 : -1;
            }
            const bool left = path.avoid_side > 0;
            const Core::Fixed32 slide_x = left ? Core::Fixed32(0) - push_y : push_y;
            const Core::Fixed32 slide_y = left ? push_x : Core::Fixed32(0) - push_x;

            steer_x = steer_x + push_x * kAvoidWeight + slide_x * kSlideWeight;
            steer_y = steer_y + push_y * kAvoidWeight + slide_y * kSlideWeight;
        } else {
            path.avoid_side = 0;
        }

        // Avoidance can cancel the heading exactly; fall back to it rather than
        // standing still.
        if (steer_x.value == 0 && steer_y.value == 0) {
            steer_x = heading.x;
            steer_y = heading.y;
        }

        Core::Fixed32 step = StepLength(registry, entity, fixed_dt_ms);
        if (path.speed_cap.value > 0) {
            step = Core::FixedMath::Min(
                step, path.speed_cap * Core::Fixed32(fixed_dt_ms) / Core::Fixed32(1000));
        }
        const auto velocity =
            Core::FixedMath::NormalizeAndScale(steer_x, steer_y, step);
        const Physics::Layer layer = Navigation::LayerOf(registry, grid, entity);
        StepWithSliding(grid, layer, transform, velocity.x, velocity.y);

        // Face the way the unit actually travels, including the avoidance bend,
        // and in screen terms -- otherwise the animation angle disagrees with
        // the direction of motion.
        if (velocity.x.value != 0 || velocity.y.value != 0) {
            sprite.heading =
                Core::FixedMath::WorldVectorToHeading(velocity.x, velocity.y);
        }

        // Progress is getting closer to the waypoint. A new waypoint starts a
        // fresh measurement.
        const Core::Fixed32 remaining = Core::FixedMath::Distance(
            waypoint.x - transform.position.x, waypoint.y - transform.position.y);
        if (path.closest_for != path.current_waypoint_index ||
            remaining + step * kProgressShare <= path.closest) {
            path.closest = remaining;
            path.closest_for = path.current_waypoint_index;
            path.blocked_ticks = 0;
            continue;
        }

        // Getting nowhere. Close to the destination that means the spot is
        // taken and the unit has arrived as near as it can; further out it
        // means the route has stopped working, so rebuild it.
        ++path.blocked_ticks;
        if (path.blocked_ticks < kTicksBeforeRepath) continue;

        if (path.has_goal) {
            const Core::Fixed32 to_goal = Core::FixedMath::Distance(
                path.goal.x - transform.position.x,
                path.goal.y - transform.position.y);
            if (to_goal <= kCrowdedArrive) {
                FinishOrder(path);
                continue;
            }
        }
        if (path.repaths < kMaxRepaths && Repath(grid, layer, path, transform)) continue;
        if (path.blocked_ticks > kTicksBeforeGivingUp) FinishOrder(path);
    }

    // Units standing still still need to unstack, but only that: moving units
    // resolve crowding by steering above.
    auto idle = registry.view<Components::Transform, Components::Animation>();
    for (auto entity : idle) {
        if (registry.all_of<Components::Building>(entity)) continue;
        if (const auto* path = registry.try_get<Components::Path>(entity)) {
            if (path->current_waypoint_index < path->waypoints.size()) continue;
        }
        // A villager swinging a hammer is anchored on the tile it claimed;
        // nudging it off that tile would restart its approach.
        if (const auto* worker = registry.try_get<Components::Worker>(entity)) {
            if (worker->working) continue;
        }
        auto found = crowd.by_entity.find(
            static_cast<uint32_t>(entt::to_integral(entity)));
        if (found == crowd.by_entity.end()) continue;

        Core::Fixed32 push_x(0);
        Core::Fixed32 push_y(0);
        crowd.Separation(found->second, true, push_x, push_y);
        if (push_x.value == 0 && push_y.value == 0) continue;

        auto& transform = idle.get<Components::Transform>(entity);
        const auto nudge =
            Core::FixedMath::NormalizeAndScale(push_x, push_y, kUnstackPush);
        StepWithSliding(grid, Navigation::LayerOf(registry, grid, entity), transform,
                        nudge.x, nudge.y);
    }
}

} // namespace ECS::Systems
