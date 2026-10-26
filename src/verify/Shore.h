#pragma once

// Helpers the water scenarios share: finding a shore to build a dock on, open
// sea, a player's units. Each scenario still builds its own world; these only
// look things up in it.
#include <cstdlib>

#include <entt/entt.hpp>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Buildings.h"
#include "ecs/Navigation.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace Verify::Shore {

constexpr int kTicksPerSecond = 1000 / 16;

inline ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x), Core::Fixed32(y)};
}

inline Core::Fixed32 Apart(ECS::Components::Vector2D a, ECS::Components::Vector2D b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

// The sea's layer: where a galley moves.
inline Physics::Layer Sea(const ECS::Simulation& sim) {
    const auto* galley = Core::Rules::Unit(Core::ConfigManager::Get().GetUnitIdByName("SGALY"));
    return galley != nullptr
        ? const_cast<ECS::Simulation&>(sim).GetGrid().LayerOf(galley->terrain_restriction)
        : Physics::kLand;
}

// The tile nearest (x, y) where a building of `unit_id` may stand, searched
// in widening rings; false if there is none within `radius`.
inline bool NearestSpot(const Physics::Grid& grid, int unit_id, int x, int y, int radius,
                        int& out_x, int& out_y) {
    for (int r = 0; r <= radius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                if (ECS::Buildings::CanPlace(grid, unit_id, x + dx, y + dy)) {
                    out_x = x + dx;
                    out_y = y + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

// A finished building of `unit_id` for `player` at the nearest spot to its
// start that the dat allows, or null.
inline entt::entity RaiseNear(ECS::Simulation& sim, Core::PlayerId player, int unit_id,
                              int start_x, int start_y, int radius) {
    int x = 0, y = 0;
    if (!NearestSpot(sim.GetGrid(), unit_id, start_x, start_y, radius, x, y)) return entt::null;
    return ECS::Buildings::Place(sim.GetRegistry(), sim.GetGrid(), unit_id, player, x, y, true);
}

// The nearest thing of the dat's class `cls` to `from` that `mover` can get
// to on its own layer: standing on, or beside, its region.
inline entt::entity NearestOfClass(const ECS::Simulation& sim, entt::entity mover, int cls,
                                   ECS::Components::Vector2D from) {
    auto& reg = const_cast<ECS::Simulation&>(sim).GetRegistry();
    const auto& grid = const_cast<ECS::Simulation&>(sim).GetGrid();
    const int region = ECS::Navigation::RegionOf(reg, grid, mover);
    const Physics::Layer layer = ECS::Navigation::LayerOf(reg, grid, mover);
    entt::entity best = entt::null;
    Core::Fixed32 nearest(1 << 20);
    auto things = reg.view<const ECS::Components::Identity, const ECS::Components::Transform>();
    for (auto e : things) {
        const auto* rules = Core::Rules::Unit(things.get<const ECS::Components::Identity>(e).unit_id);
        if (rules == nullptr || rules->cls != cls) continue;
        const auto& at = things.get<const ECS::Components::Transform>(e).position;
        if (!Pathfinding::Approachable(grid, region, Physics::TileOf(at.x), Physics::TileOf(at.y),
                                       layer)) {
            continue;
        }
        const Core::Fixed32 d = Apart(at, from);
        if (d < nearest) {
            nearest = d;
            best = e;
        }
    }
    return best;
}

} // namespace Verify::Shore
