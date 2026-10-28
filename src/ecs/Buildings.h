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

// What a building of `unit_id` put down by `player` at the tile runs into, as
// the outline, the order and the first work on a foundation all ask it:
// ground that is not open (anything that holds ground on it, begun or
// finished, seen or not), a foundation not yet begun of the player's own or
// of an ally's each way, or a unit of a player not allied with it each way
// (not for a farm). Placing it, the ground must also suit it -- its terrain,
// its slope -- and some tile of it be explored by the player; beginning it
// (`self`, the foundation, left out of the count), only what stands there
// counts.
enum class Site : uint8_t { Clear, Ground, Unexplored, Foundation, Unit };
enum class Ask : uint8_t { Placing, Beginning };
Site Check(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
           int unit_id, int tile_x, int tile_y, Ask ask, entt::entity self = entt::null);

// Whether `building` holds its ground: anything finished, and a foundation
// once worked on.
bool Begun(const entt::registry& registry, entt::entity building);

// The first work on a foundation. Taken: its ground is not clear, and it is
// left as it was. Crowded: units of its owner's side or Gaia's still stand on
// it after being told to step off. Otherwise it takes its ground, and every
// other foundation not yet begun that overlaps it, whoever's, is torn down
// with its price refunded. Its sight stays its own ground until it stands.
enum class Start : uint8_t { Begun, Taken, Crowded };
Start Begin(entt::registry& registry, Physics::Grid& grid, entt::entity site);

// Pays back what `building` cost its owner, as deleting it does: all of it for
// a foundation not yet begun, the share not yet built for one going up,
// nothing for a finished one.
void Refund(entt::registry& registry, entt::entity building);

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
