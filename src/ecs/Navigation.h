#pragma once
#include <cstdint>
#include <vector>

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

// A unit's radius: half its body's width, the dat's size.
Core::Fixed32 RadiusOf(const entt::registry& registry, entt::entity entity);

// Where every unit stands at the start of the tick, for routes to go round
// and arrivals to stop against: its middle, its radius and whether it is on
// its way somewhere, filed by the tile it stands on.
struct Body {
    entt::entity entity = entt::null;
    Core::Fixed32 x, y;
    Core::Fixed32 radius;
    bool moving = false;
};

struct Traffic {
    int width = 0;
    int height = 0;
    std::vector<Body> bodies;       // by tile, then by entity
    std::vector<int32_t> first;     // per tile, its first body; one past the last tile at the end
};

// Takes the tick's survey: at the start of every tick, before anything plans
// a route or takes a step.
void Survey(entt::registry& registry, const Physics::Grid& grid);

// The tick's survey; null before the first.
const Traffic* TrafficOf(const entt::registry& registry);

// The units of a group sent off together, while their routes are planned:
// they all set off at once, so none plans round the rest as though they stood
// still. Sorted; null when no group is being sent.
void Marching(const std::vector<entt::entity>* group);

// `entity`'s route from `from` to `to`, on its own layer, for its body
// (Pathfinding::AStar::FindRoute): round the units standing still about the
// way where that is short, keeping off walls and trees. Empty when there is
// none.
//
// With a `target`, the walk ends as soon as the unit's box comes within
// `range` of the target's (Components::Path::target).
Components::Path Route(const entt::registry& registry, const Physics::Grid& grid,
                       entt::entity entity, Components::Vector2D from,
                       Components::Vector2D to, entt::entity target = entt::null,
                       Core::Fixed32 range = Core::Fixed32(0));

// How far apart a unit standing at `at` is from `target`, box to box: the
// unit's box its radius each way, the target's Combat::BoxOf.
Core::Fixed32 GapAt(const entt::registry& registry, entt::entity entity, Components::Vector2D at,
                    entt::entity target);

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
