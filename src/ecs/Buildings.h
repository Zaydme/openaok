#pragma once
#include <entt/entt.hpp>

#include "core/ConfigManager.h"
#include "core/Types.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// A building's life: placed as a foundation, raised, finished, and torn down.
//
// Each step changes the grid under the footprint, the art, the owner's
// resources and the owner's technologies together. Footprint geometry is in
// ecs/BuildingGeometry.
//
// Everything a building is comes from the owner's copy of the unit rules: its
// hit points, its art, what it contributes while it stands. A House placed in
// the Feudal Age is the owner's slot 70, which by then holds the Feudal House.
namespace ECS::Buildings {

// The unit a build order for `unit_id` actually puts up. A few buttons stand
// for a unit that only places another -- the Town Center's button is
// "RTWC1X", whose stack unit is the Town Center -- and the dat says so.
int PlacedUnit(int unit_id);

// Whether every tile of `unit_id`'s footprint at the tile is open ground.
bool CanPlace(const Physics::Grid& grid, int unit_id, int tile_x, int tile_y);

// Creates a building of `unit_id` for `player` on the tile. A foundation
// (`complete` false) stands at one hit point and wears the construction art;
// a complete building gets its real art and starts contributing at once --
// the starting Town Center is placed this way. Returns null if the data does
// not know the unit.
entt::entity Place(entt::registry& registry, Physics::Grid& grid, int unit_id,
                   Core::PlayerId player, int tile_x, int tile_y, bool complete);

// Finishes a foundation: real art, the owner's headroom from it, its own
// technology researched, and -- for a farm -- its crop on the ground.
void Complete(entt::registry& registry, Physics::Grid& grid, entt::entity building);

// Once a tick, after the builders have worked: finishes every site whose
// construction is complete, and moves farm sites' ground through its stages.
void Update(entt::registry& registry, Physics::Grid& grid);

// Tears a building down: frees its ground, leaves its rubble, takes back what
// it contributed, and destroys its art. Anyone working on it stops. A wall
// segment a gate is put over goes without leaving rubble (`rubble` false).
void Destroy(entt::registry& registry, Physics::Grid& grid, entt::entity building,
             bool rubble = true);

// Moves every unit standing on `ground` to the nearest spot beside it, as
// when a foundation is laid where villagers stand.
void StepAside(entt::registry& registry, const Physics::Grid& grid, const Bounds& ground);

// The graphic one piece of a building's composition is drawn with, for its
// owner: a piece naming a unit wears what the owner's slot for that unit has
// become -- an age upgrades a Town Center's annexes along with its body -- and
// a right-hand piece is the counterpart of that. -1 when nothing resolves.
int PieceGraphic(const entt::registry& registry, Core::PlayerId owner,
                 const Core::ConfigManager::Annex& piece);

// Where a unit stepping out of `building` stands: against its front wall,
// beside anyone already standing there, on the ground of the unit's own layer
// -- a ship launched from a dock comes out on the water side. False when every
// bit of wall is blocked. Trained units and the units a player starts with
// both appear here.
bool Doorstep(const entt::registry& registry, const Physics::Grid& grid,
              entt::entity building, Components::Vector2D& out,
              Physics::Layer layer = Physics::kLand);

// Rebuilds a finished building's art from its owner's current rules -- after
// an age advance, say, when the slot it stands for has been upgraded.
void RefreshArt(entt::registry& registry, entt::entity building);

} // namespace ECS::Buildings
