#pragma once
#include <utility>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Types.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"
#include "scenario/Scenario.h"

// A scenario's world: its ground, and each thing the editor placed, built the
// way the rest of the engine builds them. Simulation::Init builds a scenario
// game through Simulation::InitScenario, defined beside these; the triggers
// (ecs/Triggers) create and remove things through them.
//
// The file is in the scenario map frame, a quarter turn from the grid's (the
// turn ecs/BuildingGeometry's FromDatAxes makes for the dat's offsets): the
// file's x runs up the screen to the right and its y down it to the right,
// where the grid's x runs down to the right and its y down to the left. Cliff
// pieces are drawn for the file's frame. The world is built from the file
// turned (Turned): a point (x, y) of the file is the grid's (y, width - x), a
// tile (x, y) the grid's tile (y, width - 1 - x), `width` the file's.
//
// Positions here are the engine's: a tile's centre on its whole number, half
// a tile back from the file's (a unit in the middle of the file's tile 10 is
// at 10 across the grid). A building stands on the tile its centre is in,
// which for an even footprint is the tile up and left of its centre -- the
// anchoring ecs/BuildingGeometry gives every building.
namespace ECS::ScenarioWorld {

// A point of the file -- its frame, a tile t spanning [t, t + 1) -- as the
// grid's position; and a tile of the file as the grid's tile.
Components::Vector2D FromFile(Core::Fixed32 x, Core::Fixed32 y, int file_width);
std::pair<int, int> TileFromFile(int x, int y, int file_width);

// The file as the grid lays it out: its size, ground, every unit's place,
// the cameras and every trigger's location and area turned onto the grid's
// frame (a unit's place still in the span of its tile, half a tile on from
// the grid's position). A unit's rotation stays the file's; FacingOf turns it.
Scenario::File Turned(const Scenario::File& file);

// The engine's ground for a dat terrain. Grounds the engine has no terrain of
// its own for are laid as the one the dat draws them as (its TerrainToDraw:
// 14 Desert as 13, 16 Old Grass as 0, 18 Bamboo, 19 Pine Forest and 20 Oak
// Forest as 10 Forest), except 28, the Water Bridge: it draws as water but
// land units cross it (terrain restriction 7 passes it), so it is laid as
// Shallows, the one ground that does both.
Core::TerrainType TerrainOf(int dat_terrain);

// Places one thing of a scenario's for `owner`: a building (finished), a
// unit, a cliff piece, or a map object -- a tree, a mine, a flag, an animal.
// `rotation` is the file's (radians for a unit), `frame` its first frame (a
// cliff's piece, a tree's shape). Null for a unit the data does not know.
entt::entity Place(entt::registry& registry, Physics::Grid& grid, int unit_id,
                   Core::PlayerId owner, Components::Vector2D at, Core::Fixed32 rotation,
                   int frame);

// Whether a map object -- not a building, not a living thing -- blocks the
// tile it stands on: anything holding a resource does (a tree, a mine, a
// fish, as a random map's), and anything the dat makes solid (obstruction 2: a
// rock, a mountain); a rug, a skeleton, a flag does not.
bool Blocks(int unit_id);

// Takes something out of the world with no death -- a Remove Object: a
// building comes down, a unit and whoever it carries vanish, a map object
// frees its tile.
void Remove(entt::registry& registry, Physics::Grid& grid, entt::entity entity);

} // namespace ECS::ScenarioWorld
