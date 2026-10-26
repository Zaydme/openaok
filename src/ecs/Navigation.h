#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Which ground a unit moves on, and its routes over it.
//
// A unit's terrain restriction, in its owner's rules, says where it may go;
// the grid keeps one passability layer per restriction the movers in the data
// use (physics/Grid). This module says which layer is a unit's, for every
// system that routes, steps or places it.
//
// Every gameplay route is asked for through Route, not AStar::FindPathTo
// directly: the A* takes a layer, and only this module knows a unit's.
namespace ECS::Navigation {

// The layer a unit of `unit_id`, belonging to `player`, moves on.
Physics::Layer LayerOfUnit(const entt::registry& registry, const Physics::Grid& grid,
                           Core::PlayerId player, int unit_id);

// The layer `entity` moves on, keyed with its owner (physics/Grid), so its
// own side's gates open for it. Land for anything without rules.
Physics::Layer LayerOf(const entt::registry& registry, const Physics::Grid& grid,
                       entt::entity entity);

// Whether `entity` could stand at `at`: its layer allows the ground there and
// nothing stands on it.
bool CanStand(const entt::registry& registry, const Physics::Grid& grid,
              entt::entity entity, Components::Vector2D at);

// `entity`'s route from `from` to `to`, on its own layer. Empty when there is
// none (Pathfinding::AStar::FindPathTo).
Components::Path Route(const entt::registry& registry, const Physics::Grid& grid,
                       entt::entity entity, Components::Vector2D from,
                       Components::Vector2D to);

// Where `entity` should make for when sent to `to`: `to` itself if it can
// stand there, else the nearest point it can reach on its own ground -- a
// transport sent at the land comes to the shore, a villager sent at the sea
// to the water's edge -- searched out to a few tiles; `to` if there is none.
Components::Vector2D Toward(const entt::registry& registry, const Physics::Grid& grid,
                            entt::entity entity, Components::Vector2D to);

// The region of its own layer `entity` stands in, -1 for none.
int RegionOf(const entt::registry& registry, const Physics::Grid& grid,
             entt::entity entity);

} // namespace ECS::Navigation
