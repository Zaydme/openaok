#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/RMSParser.h"
#include "core/Types.h"
#include "physics/Grid.h"

// A random map script's connections (<CONNECTION_GENERATION>): paths laid
// between lands so they can be reached, through forest cut back to grass or
// water forded with shallows.
//
// * `create_connect_all_players_land` joins every player's land to every
//   other's; `create_connect_teams_lands` joins team-mates' lands.
// * A path runs between two lands' origins, the way that costs least: each
//   tile entered costs its terrain's `terrain_cost` (1 for a terrain the
//   script names no cost for; a cost of 0 or less cannot be crossed).
// * Round each tile of the path, out to its terrain's `terrain_size` radius
//   (plus or minus up to its variance; 1 for a terrain the script names no
//   size for), every tile whose terrain the script names in a
//   `replace_terrain` becomes the terrain it names after it, and loses the
//   tree it stood under when the new terrain carries none.
//
// The draws (each tile's variance) come from a random stream of their own, so
// laying the connections moves nothing else the map draws.
namespace Core::Connections {

// Lays the section's connections between the players' origins (`starts`, in
// seat order, with each seat's team in `teams`, 0 for none).
void Generate(Physics::Grid& grid, entt::registry& registry, const RMSSection& section,
              const std::vector<std::pair<int, int>>& starts, const std::vector<TeamId>& teams, uint32_t seed);

} // namespace Core::Connections
