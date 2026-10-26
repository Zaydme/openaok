#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "core/RMSParser.h"
#include "core/Random.h"
#include "core/Types.h"
#include "physics/Grid.h"

// The land pass of a random map script: `base_terrain`, `create_player_lands`
// and `create_land`.
//
// A land is a blob of one terrain grown from an origin. Every land starts as a
// square of its base size about its origin and then they all grow together, a
// tile at a time each in turn, until each has the share of the map it asked
// for (`land_percent`, or `number_of_tiles`) or has nowhere left to go. Growth
// keeps inside the land's borders -- a percentage of the map kept clear on any
// side -- raggedly, by its border fuzziness; it prefers tiles hemmed in by its
// own, by its clumping factor; and it keeps the given distance from any land
// of another zone. That last rule is what makes islands: players in zones of
// their own, kept apart, with the base terrain -- water -- between them.
//
// Player lands are one per player, each its own zone unless the script puts
// them together (`zone`, `set_zone_by_team`, `set_zone_randomly`), their
// origins spaced evenly round the map and their share of the map divided
// between them. Other lands go where
// `land_position` says, or anywhere inside their borders.
namespace Core::LandGenerator {

struct Seat {
    PlayerId player = 0;
    TeamId team = 0;
};

struct Layout {
    // Each player's origin, in seat order.
    std::vector<std::pair<int, int>> starts;
    // Per tile, the land that took it: an index into `land_ids`, or -1 for
    // the base terrain.
    std::vector<int16_t> land_of;
    // Per land, the id objects can ask for it by (`land_id`; a player land is
    // its player's number), and the player it belongs to, 0 for none.
    std::vector<int> land_ids;
    std::vector<PlayerId> land_player;
    TerrainType base_terrain = TerrainType::Grass1;

    int LandAt(int width, int x, int y) const {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
        return i < land_of.size() ? land_of[i] : -1;
    }
};

// How a tile is given its terrain -- the map generator's own painter, which
// also sets the tile's pattern cell and whether it can be walked.
using Paint = void (*)(Physics::Grid& grid, int x, int y, TerrainType terrain);

// Lays out the map's lands and paints their terrain. `rng` is the map's own.
Layout Generate(Physics::Grid& grid, const RMSSection& land, const std::vector<Seat>& seats,
                Random& rng, Paint paint);

} // namespace Core::LandGenerator
