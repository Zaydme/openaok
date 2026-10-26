#include "ecs/Navigation.h"

#include "core/FixedMath.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Players.h"
#include "ecs/components/Identity.h"
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

Components::Path Route(const entt::registry& registry, const Physics::Grid& grid,
                       entt::entity entity, Components::Vector2D from,
                       Components::Vector2D to) {
    return Pathfinding::AStar::FindPathTo(grid, from, to, LayerOf(registry, grid, entity));
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
