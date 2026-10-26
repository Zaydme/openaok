#pragma once
#include "physics/Grid.h"
#include "core/ConfigManager.h"
#include "core/RMSParser.h"
#include "core/Random.h"
#include "core/Types.h"
#include <vector>
#include <string>
#include <cstdint>
#include <entt/entt.hpp>

namespace Core {

// Settings for map generation: the seats, and the figures the procedural
// generators use.
struct MapGenConfig {
    int width = 256;
    int height = 256;
    int num_players = 2;
    // Each seat's player and team, in seat order: a script can put team-mates
    // on one landmass (`set_zone_by_team`).
    std::vector<PlayerId> players;
    std::vector<TeamId> teams;
    int base_margin = 20;     // How far bases should be from map edge
    float forest_density = 0.35f;
    float water_density = 0.15f;
    float elevation_roughness = 0.4f;
    
    // Resource spawning per player
    int gold_mines = 3;
    int stone_mines = 2;
    int forage_bushes = 1;
    int sheep_flocks = 2;
    int deer_herds = 1;
    int boars = 2;
    int total_relics = 5;
};

class MapGenerator {
public:
    static void Generate(Physics::Grid& grid, uint32_t seed);
    // The grid's passability layers from the dat (physics/Grid): land by the
    // villager's terrain restriction, then one for each other set of grounds
    // the data's movers' restrictions allow -- the sea, the wild animals'.
    // A grid built for a check rather than by a script asks for them too.
    static void SetUpLayers(Physics::Grid& grid);
    // Builds a map by running a random map script from gamedata.drs, parsed
    // by RMSParser. Terrains and objects come from the script and the tables
    // it names.
    static std::vector<std::pair<int, int>> GenerateFromScript(
        Physics::Grid& grid, entt::registry& registry, uint32_t seed,
        const RMSScript& script, const MapGenConfig& config);
    
    // A noise-based map, with no script.
    static std::vector<std::pair<int, int>> GenerateProcedural(Physics::Grid& grid, entt::registry& registry, uint32_t seed, const MapGenConfig& config);

    // For a scenario's map (ecs/ScenarioStart.cpp), the two steps a
    // script's map takes per tile and per object, so a scenario's
    // ground and objects are laid exactly as a random map's: a tile's ground
    // with its cell of the terrain's tiling pattern, and an object's art,
    // identity, and -- for a living thing -- hit points and owner.
    static void Paint(Physics::Grid& grid, int x, int y, TerrainType terrain);
    static void PlaceMapObject(entt::registry& registry, entt::entity entity,
                               const ConfigManager::ObjectArt& art, Random& rng);
};

} // namespace Core
