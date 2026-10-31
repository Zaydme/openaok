#include "ecs/Navigation.h"

#include <algorithm>

#include "core/FixedMath.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Players.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "pathfinding/ApproachPlanner.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Navigation {

Physics::Layer LayerOfUnit(const entt::registry& registry, const Physics::Grid& grid,
                           Core::PlayerId player, int unit_id) {
    const Core::Rules::UnitRules* rules = Players::UnitOf(registry, player, unit_id);
    return rules != nullptr ? grid.LayerOf(rules->terrain_restriction) : Physics::kLand;
}

Physics::Layer LayerOf(const entt::registry& registry, const Physics::Grid& grid,
                       entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    if (identity == nullptr) return Physics::kLand;
    const auto* owner = registry.try_get<Components::Owner>(entity);
    const Core::PlayerId player = owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
    // Keyed with its owner: a gate opens to its owner and allies, and this
    // is how every route, step and standing spot learns whose unit asks.
    return Physics::Keyed(
        LayerOfUnit(registry, grid, Players::RulesPlayer(registry, entity), identity->unit_id),
        player);
}

bool CanStand(const entt::registry& registry, const Physics::Grid& grid,
              entt::entity entity, Components::Vector2D at) {
    return grid.Passable(LayerOf(registry, grid, entity), Physics::TileOf(at.x),
                         Physics::TileOf(at.y));
}

Core::Fixed32 RadiusOf(const entt::registry& registry, entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    const Core::Rules::UnitRules* rules =
        identity != nullptr ? Players::RulesFor(registry, entity, identity->unit_id) : nullptr;
    return rules != nullptr ? rules->size_x : Core::Fixed32(0);
}

void Survey(entt::registry& registry, const Physics::Grid& grid) {
    auto& traffic = registry.ctx().emplace<Traffic>();
    traffic.width = grid.GetWidth();
    traffic.height = grid.GetHeight();
    traffic.bodies.clear();
    // Every unit: what has an animation and is not a building, and still has
    // a body -- not one dying.
    auto units = registry.view<const Components::Transform, const Components::Animation>();
    for (auto e : units) {
        if (registry.any_of<Components::Building, Components::Dying, Components::Corpse>(e)) continue;
        const auto& at = units.get<const Components::Transform>(e).position;
        if (!grid.InBounds(Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
        const auto* path = registry.try_get<Components::Path>(e);
        traffic.bodies.push_back({e, at.x, at.y, RadiusOf(registry, e),
                                  path != nullptr && path->current_waypoint_index < path->waypoints.size()});
    }
    // By tile, then by entity: sorted on one key each, tile above entity.
    const int w = traffic.width;
    static std::vector<std::pair<uint64_t, uint32_t>> keys;   // key, body
    static std::vector<Body> unsorted;
    keys.resize(traffic.bodies.size());
    for (size_t i = 0; i < traffic.bodies.size(); ++i) {
        const Body& b = traffic.bodies[i];
        const auto tile = static_cast<uint32_t>(Physics::TileOf(b.y) * w + Physics::TileOf(b.x));
        keys[i] = {(static_cast<uint64_t>(tile) << 32) | entt::to_integral(b.entity), static_cast<uint32_t>(i)};
    }
    std::sort(keys.begin(), keys.end());
    unsorted.swap(traffic.bodies);
    traffic.bodies.resize(unsorted.size());
    for (size_t i = 0; i < keys.size(); ++i) traffic.bodies[i] = unsorted[keys[i].second];
    const size_t tiles = static_cast<size_t>(w) * traffic.height;
    traffic.first.assign(tiles + 1, 0);
    for (const auto& k : keys) ++traffic.first[static_cast<size_t>(k.first >> 32) + 1];
    for (size_t i = 1; i <= tiles; ++i) traffic.first[i] += traffic.first[i - 1];
}

const Traffic* TrafficOf(const entt::registry& registry) {
    return registry.ctx().find<Traffic>();
}

namespace {

// How far past the box round the start and the goal the units standing about
// are taken in, in tiles.
constexpr int kStandingReach = 3;

} // namespace

Core::Fixed32 GapAt(const entt::registry& registry, entt::entity entity, Components::Vector2D at,
                    entt::entity target) {
    const Core::Fixed32 r = RadiusOf(registry, entity);
    const Buildings::Bounds box = Combat::BoxOf(registry, target);
    const Buildings::Bounds reach{box.min_x - r, box.min_y - r, box.max_x + r, box.max_y + r};
    return reach.DistanceFrom(at);
}

Components::Path Route(const entt::registry& registry, const Physics::Grid& grid,
                       entt::entity entity, Components::Vector2D from,
                       Components::Vector2D to, entt::entity target, Core::Fixed32 range) {
    static std::vector<Pathfinding::Standing> standing;
    standing.clear();
    if (const Traffic* traffic = TrafficOf(registry)) {
        const int x0 = std::max(0, std::min(Physics::TileOf(from.x), Physics::TileOf(to.x)) - kStandingReach);
        const int y0 = std::max(0, std::min(Physics::TileOf(from.y), Physics::TileOf(to.y)) - kStandingReach);
        const int x1 = std::min(traffic->width - 1, std::max(Physics::TileOf(from.x), Physics::TileOf(to.x)) + kStandingReach);
        const int y1 = std::min(traffic->height - 1, std::max(Physics::TileOf(from.y), Physics::TileOf(to.y)) + kStandingReach);
        for (int ty = y0; ty <= y1; ++ty) {
            const size_t row = static_cast<size_t>(ty) * traffic->width;
            for (int32_t i = traffic->first[row + x0]; i < traffic->first[row + x1 + 1]; ++i) {
                const Body& b = traffic->bodies[static_cast<size_t>(i)];
                if (b.entity == entity || b.entity == target || b.moving) continue;
                standing.push_back({b.x - b.radius, b.y - b.radius, b.x + b.radius, b.y + b.radius});
            }
        }
    }
    auto path = Pathfinding::AStar::FindRoute(grid, from, to, LayerOf(registry, grid, entity),
                                              RadiusOf(registry, entity), &standing);
    path.target = target;
    path.range = range;
    return path;
}

Components::Vector2D Toward(const entt::registry& registry, const Physics::Grid& grid,
                            entt::entity entity, Components::Vector2D to) {
    if (CanStand(registry, grid, entity, to)) return to;
    const auto* at = registry.try_get<Components::Transform>(entity);
    if (at == nullptr) return to;
    const Physics::Layer layer = LayerOf(registry, grid, entity);
    const int region = RegionOf(registry, grid, entity);
    if (region < 0) return to;
    // The nearest tile of the unit's own ground, ring by ring out from the
    // spot -- the nearest to the unit among those as near.
    constexpr int kReach = 32;
    const int cx = Physics::TileOf(to.x);
    const int cy = Physics::TileOf(to.y);
    for (int r = 1; r <= kReach; ++r) {
        int best_x = 0, best_y = 0;
        Core::Fixed32 best_d{};
        bool found = false;
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x != cx - r && x != cx + r && y != cy - r && y != cy + r) continue;
                if (Pathfinding::RegionAt(grid, x, y, layer) != region) continue;
                const Core::Fixed32 d = Core::FixedMath::Distance(Core::Fixed32(x) - at->position.x,
                                                                  Core::Fixed32(y) - at->position.y);
                if (!found || d < best_d) {
                    found = true;
                    best_d = d;
                    best_x = x;
                    best_y = y;
                }
            }
        }
        if (!found) continue;
        // On that ring round the spot, the approach planner's own point
        // facing that tile: as near the spot as the ground allows, not a
        // tile's middle.
        Pathfinding::ApproachRequest ask;
        ask.target = Buildings::TileBounds(to);
        ask.from = {Core::Fixed32(best_x), Core::Fixed32(best_y)};
        ask.standoff = Core::Fixed32(r) - Core::Fixed32(4) / Core::Fixed32(10);
        ask.clearance = Core::Fixed32(1) / Core::Fixed32(2);
        ask.layer = layer;
        const auto spot = Pathfinding::FindStandingSpot(grid, ask);
        if (spot.found && Pathfinding::RegionAt(grid, Physics::TileOf(spot.spot.x),
                                                Physics::TileOf(spot.spot.y), layer) == region) {
            return spot.spot;
        }
        return {Core::Fixed32(best_x), Core::Fixed32(best_y)};
    }
    return to;
}

int RegionOf(const entt::registry& registry, const Physics::Grid& grid,
             entt::entity entity) {
    const auto* at = registry.try_get<Components::Transform>(entity);
    if (at == nullptr) return -1;
    return Pathfinding::RegionAt(grid, Physics::TileOf(at->position.x),
                                 Physics::TileOf(at->position.y),
                                 LayerOf(registry, grid, entity));
}

} // namespace ECS::Navigation
