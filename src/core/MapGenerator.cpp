#include "MapGenerator.h"
#include "core/Cliffs.h"
#include "core/LandGenerator.h"
#include "core/Random.h"
#include "core/Types.h"
#include "core/RMSParser.h"
#include "core/ConfigManager.h"
#include "../ecs/components/Transform.h"
#include "../ecs/components/Sprite.h"
#include "../ecs/components/Resource.h"
#include "../ecs/components/Animation.h"
#include "../ecs/components/MinimapMarker.h"
#include "../ecs/components/Owner.h"
#include "../ecs/components/Identity.h"
#include "core/Rules.h"
#include "core/Elevation.h"
#include "../ecs/systems/AnimationSystem.h"
#include "../ecs/components/Combat.h"
#include "../ecs/components/Selectable.h"
#include "../ecs/components/Health.h"
#include <iostream>
#include <limits>
#include <random>
#include <cmath>

namespace Core {

void MapGenerator::Generate(Physics::Grid& grid, uint32_t seed) {
    int w = grid.GetWidth();
    int h = grid.GetHeight();
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto& tile = grid.GetTileMut(x, y);
            
            // Primary height noise (large-scale terrain shape)
            int32_t height = Random::OctaveNoise2D(x, y, seed, 4, 120);
            
            // Detail noise (grass/dirt variation)
            int32_t detail = Random::OctaveNoise2D(x, y, seed + 500, 2, 80);
            
            // Forest noise (huge clusters)
            int32_t forest = Random::OctaveNoise2D(x, y, seed + 1000, 3, 90);
            
            // Edge falloff
            int edge_x = std::min(x, w - 1 - x);
            int edge_y = std::min(y, h - 1 - y);
            int edge_min = std::min(edge_x, edge_y);
            int edge_zone = w / 8;
            int edge_factor = (edge_min >= edge_zone) ? 1024 : (edge_min * 1024) / std::max(1, edge_zone);
            
            int32_t effective = (height * 600 + edge_factor * 400) / 1000;
            
            // === Classify terrain into massive cohesive biomes ===
            if (effective < -100) {
                tile.terrain = TerrainType::DeepWater;
                tile.is_walkable = false;
            } else if (effective < 50) {
                tile.terrain = TerrainType::ShallowWater;
                tile.is_walkable = false;
            } else if (effective < 150) {
                tile.terrain = TerrainType::Sand;
                tile.is_walkable = true;
            } else {
                // Landmass
                if (detail > 300) {
                    tile.terrain = TerrainType::Dirt1;
                } else if (detail > 100) {
                    tile.terrain = TerrainType::Grass2;
                } else {
                    tile.terrain = TerrainType::Grass1;
                }
                tile.is_walkable = true;
                
                // Massive Forest clusters
                if (forest > 150 && effective > 200 && tile.terrain != TerrainType::Dirt1) {
                    tile.has_tree = true;
                    tile.terrain = TerrainType::Forest;
                }
            }
        }
    }
    
    // Pass 2: Beach Generation (Sand near water)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto& tile = grid.GetTileMut(x, y);
            if (tile.terrain == TerrainType::Grass1 || tile.terrain == TerrainType::Grass2 || tile.terrain == TerrainType::Grass3 || tile.terrain == TerrainType::Dirt1) {
                bool near_water = false;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (grid.InBounds(x + dx, y + dy)) {
                            auto t = grid.GetTile(x + dx, y + dy).terrain;
                            if (t == TerrainType::ShallowWater || t == TerrainType::DeepWater) {
                                near_water = true;
                                break;
                            }
                        }
                    }
                    if (near_water) break;
                }
                if (near_water) {
                    tile.terrain = TerrainType::Sand;
                    tile.has_tree = false; // Remove trees from beach
                }
            }
        }
    }
    
    // Carve a clearing around map center for the starting base
    int cx = w / 2;
    int cy = h / 2;
    int clear_radius = w / 10;
    int clear_r_sq = clear_radius * clear_radius;
    
    for (int y = cy - clear_radius; y <= cy + clear_radius; ++y) {
        for (int x = cx - clear_radius; x <= cx + clear_radius; ++x) {
            if (!grid.InBounds(x, y)) continue;
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy < clear_r_sq) {
                auto& tile = grid.GetTileMut(x, y);
                tile.terrain = TerrainType::Grass1;
                tile.has_tree = false;
                tile.is_walkable = true;
                tile.elevation = 0;
            }
        }
    }
}

namespace {

// How much ground around a start is cleared for the Town Center (four tiles
// across) and the units that begin around it, with room to walk.
constexpr int kStartClearRadius = 6;

// Which frame of a terrain set a tile draws. Each set in terrain.drs is a
// square tiling pattern (frame counts 100, 64, 36, 9) whose pieces line up in
// one arrangement only: the tile at (x, y) draws the pattern's cell for that
// position. The row runs the opposite way to the column because the isometric
// grid's y axis points up-left on screen while the pattern is stored top-down.
uint8_t VariantFor(int x, int y, int frame_count) {
    if (frame_count <= 1) return 0;
    int period = 1;
    while ((period + 1) * (period + 1) <= frame_count) ++period;

    const int column = ((x % period) + period) % period;
    const int row = ((-y % period) + period) % period;
    return static_cast<uint8_t>(column * period + row);
}

void PaintTile(Physics::Grid& grid, int x, int y, TerrainType terrain) {
    if (!grid.InBounds(x, y)) return;
    const auto& art = ConfigManager::Get().GetTerrainArt(terrain);
    Physics::Tile& tile = grid.GetTileMut(x, y);
    tile.terrain = terrain;
    tile.texture_variant = VariantFor(x, y, art.frame_count);
    tile.is_walkable = !tile.blocked && grid.LandPassable(terrain);
}

// The shore: beach along the land's edge wherever it meets water (ground land
// units cannot cross). Forest keeps its trees to the waterline.
void LayBeaches(Physics::Grid& grid) {
    auto IsWater = [&grid](TerrainType terrain) { return !grid.LandPassable(terrain); };
    const int width = grid.GetWidth();
    const int height = grid.GetHeight();
    std::vector<std::pair<int, int>> shore;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Physics::Tile& tile = grid.GetTile(x, y);
            if (IsWater(tile.terrain) || tile.has_tree || tile.terrain == TerrainType::Beach) continue;
            bool wet = false;
            for (int oy = -1; oy <= 1 && !wet; ++oy) {
                for (int ox = -1; ox <= 1 && !wet; ++ox) {
                    if ((ox | oy) == 0 || !grid.InBounds(x + ox, y + oy)) continue;
                    wet = IsWater(grid.GetTile(x + ox, y + oy).terrain);
                }
            }
            if (wet) shore.emplace_back(x, y);
        }
    }
    for (const auto& [x, y] : shore) PaintTile(grid, x, y, TerrainType::Beach);
}

// Grows one blob of `target` outward from a seed, replacing only `base` and
// never crossing anything else, until it has taken `budget` tiles: both
// `create_terrain`'s clumps and `create_land`'s landmasses. Growth walks a
// frontier, so a clump follows the shape of the ground it replaces.
//
// `spacing` is the script's `spacing_to_other_terrain_types`: no tile is taken
// within that many tiles of a ground that is neither the one being replaced
// nor the one being laid. It keeps a sea's deep water off the coast (Baltic
// lays its medium water a tile off every other ground and its deep water
// three).
int GrowClump(Physics::Grid& grid, Random& rng, TerrainType target,
              TerrainType base, bool match_any_base,
              int seed_x, int seed_y, int budget,
              const std::vector<std::pair<int, int>>& avoid, int avoid_radius,
              std::vector<std::pair<int, int>>* painted, int spacing) {
    if (budget <= 0 || !grid.InBounds(seed_x, seed_y)) return 0;

    std::vector<std::pair<int, int>> frontier{{seed_x, seed_y}};
    std::vector<uint8_t> claimed(static_cast<size_t>(grid.GetWidth()) * grid.GetHeight(), 0);
    int placed = 0;

    auto acceptable = [&](int x, int y) {
        if (!grid.InBounds(x, y)) return false;
        if (claimed[static_cast<size_t>(y) * grid.GetWidth() + x]) return false;
        if (!match_any_base && grid.GetTile(x, y).terrain != base) return false;
        for (const auto& point : avoid) {
            const int dx = x - point.first;
            const int dy = y - point.second;
            if (dx * dx + dy * dy < avoid_radius * avoid_radius) return false;
        }
        for (int oy = -spacing; oy <= spacing; ++oy) {
            for (int ox = -spacing; ox <= spacing; ++ox) {
                if (!grid.InBounds(x + ox, y + oy)) continue;
                const TerrainType near = grid.GetTile(x + ox, y + oy).terrain;
                if (near != base && near != target) return false;
            }
        }
        return true;
    };

    while (!frontier.empty() && placed < budget) {
        // Pull from a random point of the frontier so the blob spreads
        // unevenly.
        const size_t pick = rng.NextInt(static_cast<uint32_t>(frontier.size()));
        const std::pair<int, int> tile = frontier[pick];
        frontier[pick] = frontier.back();
        frontier.pop_back();

        if (!acceptable(tile.first, tile.second)) continue;
        claimed[static_cast<size_t>(tile.second) * grid.GetWidth() + tile.first] = 1;
        PaintTile(grid, tile.first, tile.second, target);
        if (painted) painted->push_back(tile);
        ++placed;

        static const int kdx[4] = {1, -1, 0, 0};
        static const int kdy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i) {
            const int nx = tile.first + kdx[i];
            const int ny = tile.second + kdy[i];
            if (acceptable(nx, ny)) frontier.emplace_back(nx, ny);
        }
    }
    return placed;
}

// Gives a map object its art. A graphic with one frame per angle is a set of
// interchangeable shapes, and the object picks one to stand as. A graphic with
// several frames per angle is an animation, and the object gets an Animation
// so the usual system plays it.
void PlaceObjectArt(entt::registry& registry, entt::entity entity,
                    const ConfigManager::ObjectArt& art, Random& rng) {
    // A map object gets an identity and is selectable, so the interface can
    // name it and say how much is left in it. Its selection priority is below
    // units and buildings.
    if (art.unit_id >= 0) {
        registry.emplace_or_replace<ECS::Components::Identity>(entity,
                                                               art.unit_id);
        registry.emplace_or_replace<ECS::Components::Selectable>(entity,
            ECS::Components::Selectable{.is_selected = false, .radius = 1,
                                        .priority = 0});

        // A living thing (dat unit type 70: a sheep, a deer, a boar) has hit
        // points; a static object (a tree, a gold mine) does not. A tree's
        // twenty dat hit points are for siege damage and are not shown.
        constexpr int kLivingUnitType = 70;
        const auto* ui = ConfigManager::Get().GetUnitInterface(art.unit_id);
        if (ui != nullptr && ui->unit_type == kLivingUnitType && ui->hp > 0) {
            registry.emplace_or_replace<ECS::Components::Health>(entity,
                                                                 ui->hp, ui->hp);
            // Gaia's: a wolf is everyone's enemy, a boar answers its hunter.
            registry.emplace_or_replace<ECS::Components::Owner>(entity, PLAYER_GAIA);
            const auto* rules = Rules::Unit(art.unit_id);
            if (rules != nullptr && rules->attack_count > 0 && rules->reload.value > 0) {
                registry.emplace_or_replace<ECS::Components::Combat>(entity);
            }
        }
    }

    ECS::Components::Sprite sprite{};
    sprite.slp_id = art.slp_id;
    sprite.width = 0;
    sprite.height = 0;

    if (art.Animated()) {
        sprite.is_file_sprite = true;
        sprite.frames_per_angle = art.frame_count;
        sprite.animation_speed = art.frame_ms > 0 ? art.frame_ms : 100;
        // Start part-way in, so a flock does not move as one body.
        sprite.current_frame = static_cast<int>(rng.NextInt(
            static_cast<uint32_t>(art.frame_count)));
        // A graphic of one angle, a flag's, has no facings to turn through.
        const auto facing = static_cast<Direction>(rng.NextInt(8));
        sprite.facing = art.angle_count > 1 ? facing : Direction::South;

        ECS::Components::Animation anim{};
        anim.slp[static_cast<int>(ECS::Components::AnimState::Idle)] = art.slp_id;
        anim.frames[static_cast<int>(ECS::Components::AnimState::Idle)] = art.frame_count;
        anim.frame_ms[static_cast<int>(ECS::Components::AnimState::Idle)] =
            sprite.animation_speed;
        anim.job = ECS::Components::VillagerJob::None;
        anim.needs_apply = false;   // the sprite is already set up
        // A living thing walks, fights and dies as well as standing: its
        // whole graphic set, from the dat, the same as any unit's.
        if (registry.all_of<ECS::Components::Health>(entity)) {
            ECS::Systems::AnimationSystem::LoadSet(
                anim, ConfigManager::Get().GetUnitDatName(art.unit_id));
            const int idle = static_cast<int>(ECS::Components::AnimState::Idle);
            if (anim.slp[idle] < 0) {
                anim.slp[idle] = art.slp_id;
                anim.frames[idle] = art.frame_count;
            }
        }
        registry.emplace<ECS::Components::Animation>(entity, anim);
    } else {
        sprite.slp_frame = static_cast<int>(rng.NextInt(
            static_cast<uint32_t>(std::max(1, art.angle_count))));
    }
    registry.emplace<ECS::Components::Sprite>(entity, std::move(sprite));

    if (art.minimap_index > 0) {
        registry.emplace<ECS::Components::MinimapMarker>(entity,
            static_cast<uint8_t>(art.minimap_index));
    }
}

// Where the players start, spaced evenly around the map. `create_player_lands`
// says how big a start area is; `random_placement` leaves where to the game.
std::vector<std::pair<int, int>> PlacePlayers(const Physics::Grid& grid,
                                              int num_players, int margin) {
    std::vector<std::pair<int, int>> starts;
    const int cx = grid.GetWidth() / 2;
    const int cy = grid.GetHeight() / 2;
    const int radius = std::max(8, std::min(cx, cy) - margin);

    // Integer positions on a ring, from a fixed 16-step table so no
    // trigonometry -- and so the same player count always lands identically.
    static const int kCos[16] = {1024, 946, 724, 392, 0, -392, -724, -946,
                                 -1024, -946, -724, -392, 0, 392, 724, 946};
    static const int kSin[16] = {0, 392, 724, 946, 1024, 946, 724, 392,
                                 0, -392, -724, -946, -1024, -946, -724, -392};
    for (int p = 0; p < num_players; ++p) {
        const int step = (p * 16) / std::max(1, num_players);
        starts.emplace_back(cx + radius * kCos[step % 16] / 1024,
                            cy + radius * kSin[step % 16] / 1024);
    }
    return starts;
}

} // namespace

void MapGenerator::Paint(Physics::Grid& grid, int x, int y, TerrainType terrain) {
    PaintTile(grid, x, y, terrain);
}

void MapGenerator::PlaceMapObject(entt::registry& registry, entt::entity entity,
                                  const ConfigManager::ObjectArt& art, Random& rng) {
    PlaceObjectArt(registry, entity, art, rng);
}

void MapGenerator::SetUpLayers(Physics::Grid& grid) {
    auto& cfg = ConfigManager::Get();
    const auto& tables = Rules::Get();
    const int terrains = static_cast<int>(TerrainType::COUNT);

    // A restriction's grounds, over the engine's terrains.
    auto grounds_of = [&](int restriction) {
        std::vector<bool> passable(static_cast<size_t>(terrains), true);
        for (int t = 0; t < terrains; ++t) {
            const int dat_id = cfg.GetTerrainArt(static_cast<TerrainType>(t)).dat_id;
            if (dat_id >= 0) passable[static_cast<size_t>(t)] = Rules::Passable(restriction, dat_id);
        }
        return passable;
    };

    // Land is the villager's, and always layer 0.
    const Rules::UnitRules* walker = Rules::Unit(cfg.GetUnitIdByName("VMBAS"));
    const int land = walker != nullptr ? walker->terrain_restriction : 7;
    std::vector<std::vector<bool>> grounds{grounds_of(land)};
    std::vector<int8_t> layer_of(tables.terrain_restrictions.size(), -1);
    if (land >= 0 && land < static_cast<int>(layer_of.size())) layer_of[static_cast<size_t>(land)] = 0;

    // Every other restriction a mover uses: a unit of the dat's that moves
    // (a speed of its own) and is not a missile. Two restrictions that allow
    // the same grounds are one layer -- a galley's, a fishing ship's and a
    // transport's are three ids in the dat and one sea.
    std::vector<bool> in_use(tables.terrain_restrictions.size(), false);
    for (const Rules::UnitRules& unit : tables.units) {
        if (!unit.valid || unit.speed.value <= 0) continue;
        if (unit.type != Rules::UnitType::Creatable && unit.type != Rules::UnitType::Combatant) {
            continue;
        }
        const int r = unit.terrain_restriction;
        if (r >= 0 && r < static_cast<int>(in_use.size())) in_use[static_cast<size_t>(r)] = true;
    }
    for (size_t r = 0; r < in_use.size(); ++r) {
        if (!in_use[r] || layer_of[r] >= 0) continue;
        const std::vector<bool> mine = grounds_of(static_cast<int>(r));
        int found = -1;
        for (size_t l = 0; l < grounds.size(); ++l) {
            if (grounds[l] == mine) found = static_cast<int>(l);
        }
        if (found < 0 && grounds.size() < static_cast<size_t>(Physics::kMaxLayers)) {
            grounds.push_back(mine);
            found = static_cast<int>(grounds.size()) - 1;
        }
        layer_of[r] = static_cast<int8_t>(found);
    }
    grid.SetLayers(grounds, layer_of);
}

std::vector<std::pair<int, int>> MapGenerator::GenerateFromScript(
        Physics::Grid& grid, entt::registry& registry, uint32_t seed,
        const RMSScript& script, const MapGenConfig& config) {
    auto& cfg = ConfigManager::Get();
    Random rng(seed ? seed : 1);

    const int width = grid.GetWidth();
    const int height = grid.GetHeight();
    const int total_tiles = width * height;

    // Which grounds each kind of mover may cross: one layer per terrain
    // restriction the data's movers use (physics/Grid), land first -- the
    // restriction a villager walks by.
    SetUpLayers(grid);

    // --- Land ------------------------------------------------------------
    // Its own module: lands grown together, kept apart by zone (core/LandGenerator).
    TerrainType base_terrain = TerrainType::Grass1;
    int base_size = 9;
    std::vector<std::pair<int, int>> player_starts;
    LandGenerator::Layout lands;
    if (const RMSSection* land = script.GetSection("LAND_GENERATION")) {
        std::vector<LandGenerator::Seat> seats;
        for (int s = 0; s < config.num_players; ++s) {
            LandGenerator::Seat seat;
            seat.player = s < static_cast<int>(config.players.size())
                ? config.players[static_cast<size_t>(s)] : static_cast<PlayerId>(s + 1);
            seat.team = s < static_cast<int>(config.teams.size())
                ? config.teams[static_cast<size_t>(s)] : 0;
            seats.push_back(seat);
        }
        lands = LandGenerator::Generate(grid, *land, seats, rng, PaintTile);
        base_terrain = lands.base_terrain;
        player_starts = lands.starts;
        for (const RMSCommand& command : land->commands) {
            if (command.name == "create_player_lands") {
                base_size = command.Attribute("base_size", base_size);
            }
        }
    } else {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) PaintTile(grid, x, y, base_terrain);
        }
        player_starts = PlacePlayers(grid, config.num_players, config.base_margin);
    }

    // --- Terrain ---------------------------------------------------------
    if (const RMSSection* terrain = script.GetSection("TERRAIN_GENERATION")) {
        for (const RMSCommand& command : terrain->commands) {
            if (command.name != "create_terrain") continue;

            TerrainType target;
            if (!cfg.TerrainForRmsId(command.Value(), target)) continue;
            TerrainType on = base_terrain;
            cfg.TerrainForRmsId(command.Attribute("base_terrain", -1), on);

            const int percent = command.Attribute("land_percent", 0);
            int tiles = command.Attribute("number_of_tiles", 0);
            int clumps = std::max(1, command.Attribute("number_of_clumps", 1));
            const int spacing = std::max(0, command.Attribute("spacing_to_other_terrain_types", 0));
            // A count of tiles or of clumps grows with the map when the
            // script says so, against a 100 by 100 map: Arabia's ten forests
            // are fourteen on a tiny map and fifty-seven on a giant one.
            if (command.HasAttribute("set_scale_by_size")) tiles = tiles * total_tiles / Elevation::kReferenceTiles;
            if (command.HasAttribute("set_scale_by_groups")) {
                clumps = std::max(1, clumps * total_tiles / Elevation::kReferenceTiles);
            }
            if (percent > 0) tiles = total_tiles * percent / 100;
            if (tiles <= 0) continue;

            // The script gives a share of the map (or a number of tiles) and
            // a number of blobs to break it into; the budget per blob follows
            // from the two.
            const int budget = std::max(1, tiles / clumps);
            const std::vector<std::pair<int, int>>& avoid =
                command.HasAttribute("set_avoid_player_start_areas")
                    ? player_starts : std::vector<std::pair<int, int>>{};
            const int avoid_radius = avoid.empty() ? 0 : base_size + 4;

            // Ground that carries an object on every tile -- forest -- needs
            // the tiles it took, so the trees can be planted on them.
            const auto& art = cfg.GetTerrainArt(target);
            std::vector<std::pair<int, int>> painted;

            for (int c = 0; c < clumps; ++c) {
                // Try a few seeds before giving up on a blob: most of the map
                // may already be taken by earlier terrains.
                for (int attempt = 0; attempt < 24; ++attempt) {
                    const int sx = static_cast<int>(rng.NextInt(static_cast<uint32_t>(width)));
                    const int sy = static_cast<int>(rng.NextInt(static_cast<uint32_t>(height)));
                    if (grid.GetTile(sx, sy).terrain != on) continue;
                    if (GrowClump(grid, rng, target, on, false, sx, sy, budget,
                                  avoid, avoid_radius, &painted, spacing) > 0) {
                        break;
                    }
                }
            }

            if (art.tree_unit_id < 0) continue;
            const ConfigManager::ObjectArt tree = cfg.GetObjectArt(art.tree_unit_id);
            if (!tree.Valid()) continue;
            const ConfigManager::GaiaObject* wood = cfg.GetGaiaObject(art.tree_unit_id);
            for (const auto& tile : painted) {
                grid.GetTileMut(tile.first, tile.second).has_tree = true;
                grid.SetWalkable(tile.first, tile.second, false);
                auto entity = registry.create();
                registry.emplace<ECS::Components::Transform>(entity,
                    ECS::Components::Transform{
                        .position = {Core::Fixed32(tile.first), Core::Fixed32(tile.second)},
                        .previous_position = {Core::Fixed32(tile.first), Core::Fixed32(tile.second)}
                    });
                PlaceObjectArt(registry, entity, tree, rng);
                if (wood && wood->resource >= 0) {
                    registry.emplace<ECS::Components::Resource>(entity,
                        ECS::Components::Resource{
                            .type = static_cast<ResourceType>(wood->resource),
                            .current_amount = wood->amount,
                        });
                }
            }
        }
    }

    // --- Shore -----------------------------------------------------------
    LayBeaches(grid);

    // --- Hills -----------------------------------------------------------
    // On their own random stream; the start areas are kept flat. Run before
    // the cliffs (the elevation module's priority is 1.5, the cliffs' 1.75):
    // a cliff only runs over ground of one height.
    Elevation::Generate(grid, script, seed, player_starts, kStartClearRadius + 4);

    // --- Cliffs ----------------------------------------------------------
    // On a random stream of their own, so adding cliffs moves nothing else
    // (what they block still moves the objects placed after them). The
    // stream's starting value is arbitrary.
    if (const RMSSection* cliffs = script.GetSection("CLIFF_GENERATION")) {
        Random cliff_rng((static_cast<uint64_t>(seed) ^ 0xC11FF5EEEull) | 1u);
        Cliffs::Generate(grid, registry, *cliffs, player_starts, cliff_rng);
    }

    // --- Objects ---------------------------------------------------------
    // Room kept about objects placed with min_distance_group_placement, from
    // everything placed after them; and the tiles objects already stand on.
    struct Zone {
        int x = 0, y = 0, r = 0;
    };
    std::vector<Zone> kept;
    std::vector<uint8_t> occupied(static_cast<size_t>(total_tiles), 0);
    if (const RMSSection* objects = script.GetSection("OBJECTS_GENERATION")) {
        for (const RMSCommand& command : objects->commands) {
            if (command.name != "create_object") continue;
            // A script places the player's starting Town Center, villagers and
            // scout through the same command as scenery. An object placed once
            // per player belongs to that player unless the script forces it
            // to Gaia; anything else (gold beside a start, relics, wolves) is
            // the map's. Players' own units are spawned elsewhere, so only the
            // map's content is placed here.
            const bool belongs_to_a_player =
                command.HasAttribute("set_place_for_every_player") &&
                !command.HasAttribute("set_gaia_object_only");
            if (belongs_to_a_player) continue;
            const int unit_id = command.Value();
            // Looked up by the unit id the script names.
            const ConfigManager::ObjectArt art = cfg.GetObjectArt(unit_id);
            if (!art.Valid()) continue;
            const ConfigManager::GaiaObject* worth = cfg.GetGaiaObject(unit_id);

            // A script groups objects only when it says so (gold and stone a
            // tight rosette, sheep and deer a loose herd); otherwise each
            // object stands on its own. `number_of_groups` groups each of
            // `number_of_objects`: Arabia's far sheep are two pairs.
            const bool grouped = command.HasAttribute("set_tight_grouping") ||
                                 command.HasAttribute("set_loose_grouping") ||
                                 command.HasAttribute("group_placement_radius") ||
                                 command.HasAttribute("number_of_groups");
            const int objects_each = std::max(1, command.Attribute("number_of_objects", 1));
            int groups = grouped ? std::max(1, command.Attribute("number_of_groups", 1)) : objects_each;
            const int per_group = grouped ? objects_each : 1;
            // The number of groups -- of objects, when there are no groups --
            // grows with the map (against a 100 by 100 map) or is one lot per
            // player, when the script says so: Arabia's thirty oaks are
            // forty-three on a tiny map.
            if (command.HasAttribute("set_scaling_to_map_size")) {
                groups = std::max(1, groups * total_tiles / Elevation::kReferenceTiles);
            }
            if (command.HasAttribute("set_scaling_to_player_number")) {
                groups *= std::max(1, static_cast<int>(player_starts.size()));
            }
            // Each group's count varies by up to this, one less upward.
            const int variance = grouped ? std::max(0, command.Attribute("group_variance", 0)) : 0;
            const int radius = std::max(1, command.Attribute("group_placement_radius", 3));
            const bool tight = command.HasAttribute("set_tight_grouping");
            const bool per_player = command.HasAttribute("set_place_for_every_player");
            const int min_distance = command.Attribute("min_distance_to_players", 0);
            const int max_distance = command.Attribute("max_distance_to_players", 0);
            // Room kept about each object (or group's centre) placed: from
            // everything after it, or only from the rest of this command.
            const int keep_away = std::max(0, command.Attribute("min_distance_group_placement", 0));
            const int keep_apart = std::max(0, command.Attribute("temp_min_distance_group_placement", 0));
            // Despite its name, how far the object keeps from ground it may
            // not stand on: gold off the coast, deep fish off the beach.
            const int shore_gap = std::max(0, command.Attribute("max_distance_to_other_zones", 0));
            const int on_land = command.Attribute("place_on_specific_land_id", -1);

            const Rules::UnitRules* object_rules = Rules::Unit(unit_id);
            const int restriction = object_rules != nullptr ? object_rules->terrain_restriction : -1;
            TerrainType on_terrain;
            const bool restrict_terrain =
                cfg.TerrainForRmsId(command.Attribute("terrain_to_place_on", -1),
                                    on_terrain);

            // Where the object itself may be, by its own terrain restriction
            // -- a fish on water, a tree on grass and never on the beach --
            // on the ground the script names, if it names one, and not on
            // anything already placed.
            auto fits = [&](int x, int y) {
                if (!grid.InBounds(x, y) || occupied[static_cast<size_t>(y * width + x)]) return false;
                const Physics::Tile& tile = grid.GetTile(x, y);
                if (tile.blocked) return false;
                if (!Rules::Passable(restriction, cfg.GetTerrainArt(tile.terrain).dat_id)) return false;
                return !restrict_terrain || tile.terrain == on_terrain;
            };
            std::vector<Zone> apart;
            // A group's centre: on ground the object may take, clear of the
            // room others keep, and as far from forbidden ground as asked.
            auto open = [&](int x, int y) {
                if (!fits(x, y)) return false;
                for (const std::vector<Zone>* list : {&kept, &apart}) {
                    for (const Zone& z : *list) {
                        const int dx = x - z.x, dy = y - z.y;
                        if (dx * dx + dy * dy < z.r * z.r) return false;
                    }
                }
                for (int dy = -shore_gap; dy <= shore_gap && shore_gap > 0; ++dy) {
                    for (int dx = -shore_gap; dx <= shore_gap; ++dx) {
                        if (dx * dx + dy * dy > shore_gap * shore_gap || !grid.InBounds(x + dx, y + dy)) continue;
                        const auto& ground = grid.GetTile(x + dx, y + dy);
                        if (!Rules::Passable(restriction, cfg.GetTerrainArt(ground.terrain).dat_id)) return false;
                    }
                }
                return true;
            };
            auto nearest_start = [&](int x, int y) {
                int best = std::numeric_limits<int>::max();
                for (const auto& start : player_starts) {
                    const int dx = x - start.first, dy = y - start.second;
                    best = std::min(best, dx * dx + dy * dy);
                }
                return best;
            };

            // One group: its centre found by trying spots at random, then its
            // objects about it -- a tight group the tiles nearest the centre
            // in rings, a loose one anywhere within its radius.
            auto place_group = [&](auto&& candidate) {
                int ax = 0, ay = 0;
                bool found = false;
                for (int attempt = 0; attempt < 200 && !found; ++attempt) {
                    if (!candidate(ax, ay)) continue;
                    found = open(ax, ay);
                }
                if (!found) return;
                int count = per_group;
                if (variance > 0) {
                    count += static_cast<int>(rng.NextInt(static_cast<uint32_t>(2 * variance))) - variance;
                    count = std::max(1, count);
                }
                std::vector<std::pair<int, int>> nearby;
                if (tight) {
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) nearby.emplace_back(dx, dy);
                    }
                    std::stable_sort(nearby.begin(), nearby.end(),
                        [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                            return a.first * a.first + a.second * a.second <
                                   b.first * b.first + b.second * b.second;
                        });
                }
                size_t next_slot = 0;
                for (int i = 0; i < count; ++i) {
                    int px = ax, py = ay;
                    bool spot = i == 0;
                    for (int attempt = 0; attempt < 64 && !spot; ++attempt) {
                        if (tight) {
                            if (next_slot >= nearby.size()) break;
                            px = ax + nearby[next_slot].first;
                            py = ay + nearby[next_slot].second;
                            ++next_slot;
                        } else {
                            px = ax + static_cast<int>(rng.NextInt(static_cast<uint32_t>(radius * 2 + 1))) - radius;
                            py = ay + static_cast<int>(rng.NextInt(static_cast<uint32_t>(radius * 2 + 1))) - radius;
                        }
                        spot = fits(px, py);
                    }
                    if (!spot) continue;
                    if (tight && i == 0) next_slot = 1;
                    occupied[static_cast<size_t>(py * width + px)] = 1;
                    auto entity = registry.create();
                    registry.emplace<ECS::Components::Transform>(entity,
                        ECS::Components::Transform{
                            .position = {Core::Fixed32(px), Core::Fixed32(py)},
                            .previous_position = {Core::Fixed32(px), Core::Fixed32(py)}
                        });
                    PlaceObjectArt(registry, entity, art, rng);
                    // A tree, a mine or a bush blocks the tile it stands on;
                    // a living thing walks, and blocks nothing.
                    if (!registry.all_of<ECS::Components::Health>(entity)) {
                        grid.SetWalkable(px, py, false);
                    }
                    // What it is worth, straight from the Gaia unit table: a
                    // gold mine holds 800, a forage bush 125, a boar 340.
                    if (worth && worth->resource >= 0) {
                        registry.emplace<ECS::Components::Resource>(entity,
                            ECS::Components::Resource{
                                .type = static_cast<ResourceType>(worth->resource),
                                .current_amount = worth->amount,
                            });
                    }
                }
                if (keep_away > 0) kept.push_back({ax, ay, keep_away});
                if (keep_apart > 0) apart.push_back({ax, ay, keep_apart});
            };

            if (on_land >= 0) {
                // On each land the script gave that id, anywhere on it.
                for (size_t land_index = 0; land_index < lands.land_ids.size(); ++land_index) {
                    if (lands.land_ids[land_index] != on_land) continue;
                    for (int g = 0; g < groups; ++g) {
                        place_group([&](int& x, int& y) {
                            x = static_cast<int>(rng.NextInt(static_cast<uint32_t>(width)));
                            y = static_cast<int>(rng.NextInt(static_cast<uint32_t>(height)));
                            if (lands.LandAt(width, x, y) != static_cast<int>(land_index)) return false;
                            const int d = nearest_start(x, y);
                            return d >= min_distance * min_distance;
                        });
                    }
                }
            } else if (per_player) {
                // For each player, between its distances from that player's
                // start (anywhere beyond the least, when no most is given).
                for (const auto& start : player_starts) {
                    const int reach = max_distance > 0 ? max_distance : std::max(width, height);
                    for (int g = 0; g < groups; ++g) {
                        place_group([&](int& x, int& y) {
                            const int dx = static_cast<int>(rng.NextInt(static_cast<uint32_t>(2 * reach + 1))) - reach;
                            const int dy = static_cast<int>(rng.NextInt(static_cast<uint32_t>(2 * reach + 1))) - reach;
                            const int d = dx * dx + dy * dy;
                            x = start.first + dx;
                            y = start.second + dy;
                            return d >= min_distance * min_distance && d <= reach * reach;
                        });
                    }
                }
            } else {
                // Scattered over the map, but never nearer a start than the
                // script allows.
                for (int g = 0; g < groups; ++g) {
                    place_group([&](int& x, int& y) {
                        x = static_cast<int>(rng.NextInt(static_cast<uint32_t>(width)));
                        y = static_cast<int>(rng.NextInt(static_cast<uint32_t>(height)));
                        const int d = nearest_start(x, y);
                        return d >= min_distance * min_distance &&
                               (max_distance <= 0 || d <= max_distance * max_distance);
                    });
                }
            }
        }
    }

    // Clear each start area: every map object inside the radius is removed
    // and its tiles made walkable and treeless.
    {
        std::vector<entt::entity> in_the_way;
        auto placed = registry.view<ECS::Components::Transform,
                                    ECS::Components::Sprite>();
        for (auto entity : placed) {
            // Only the map's own contents -- Gaia's -- since no player owns
            // anything yet.
            if (const auto* owner = registry.try_get<ECS::Components::Owner>(entity)) {
                if (owner->player_id != PLAYER_GAIA) continue;
            }
            const auto& transform = placed.get<ECS::Components::Transform>(entity);
            const int ox = transform.position.x.ToInt();
            const int oy = transform.position.y.ToInt();
            for (const auto& start : player_starts) {
                const int dx = ox - start.first;
                const int dy = oy - start.second;
                if (dx * dx + dy * dy <= kStartClearRadius * kStartClearRadius) {
                    in_the_way.push_back(entity);
                    break;
                }
            }
        }
        for (auto entity : in_the_way) registry.destroy(entity);

        for (const auto& start : player_starts) {
            for (int dy = -kStartClearRadius; dy <= kStartClearRadius; ++dy) {
                for (int dx = -kStartClearRadius; dx <= kStartClearRadius; ++dx) {
                    if (dx * dx + dy * dy > kStartClearRadius * kStartClearRadius) continue;
                    if (!grid.InBounds(start.first + dx, start.second + dy)) continue;
                    grid.GetTileMut(start.first + dx, start.second + dy).has_tree = false;
                    grid.SetWalkable(start.first + dx, start.second + dy, true);
                }
            }
        }
    }

    // Tiles were written directly; rebuild what is derived from them.
    grid.WalkabilityChanged();

    std::cout << "[MapGenerator] Generated \"" << script.name << "\" for "
              << player_starts.size() << " players." << std::endl;
    return player_starts;
}

} // namespace Core

std::vector<std::pair<int, int>> Core::MapGenerator::GenerateProcedural(Physics::Grid& grid, entt::registry& registry, uint32_t seed, const MapGenConfig& config) {
    grid.Resize(config.width, config.height);
    std::mt19937 rng(seed);
    
    auto noise = [&](float x, float y, float scale) {
        x *= scale; y *= scale;
        int ix = (int)x; int iy = (int)y;
        float fx = x - ix; float fy = y - iy;
        auto hash = [](int u, int v) {
            uint32_t h = (u * 374761393U) ^ (v * 668265263U);
            h = (h ^ (h >> 13)) * 1274126177U;
            return ((h ^ (h >> 16)) & 0x7FFFFFFF) / (float)0x7FFFFFFF;
        };
        float v1 = hash(ix, iy); float v2 = hash(ix+1, iy);
        float v3 = hash(ix, iy+1); float v4 = hash(ix+1, iy+1);
        float i1 = v1 * (1-fx) + v2 * fx;
        float i2 = v3 * (1-fx) + v4 * fx;
        return i1 * (1-fy) + i2 * fy;
    };

    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            float n_biome = noise(x, y, 0.05f) * 0.6f + noise(x, y, 0.15f) * 0.4f;
            float n_forest = noise(x, y, 0.08f) * 0.7f + noise(x, y, 0.2f) * 0.3f;
            
            // Fractal water noise for organic lakes
            float n_water = noise(x, y, 0.03f) * 0.6f + noise(x, y, 0.08f) * 0.4f;
            
            auto& tile = grid.GetTileMut(x, y);
            
            if (n_biome < 0.2f) tile.terrain = Core::TerrainType::Sand;
            else if (n_biome < 0.4f) tile.terrain = Core::TerrainType::Dirt1;
            else if (n_biome < 0.5f) tile.terrain = Core::TerrainType::Dirt2;
            else if (n_biome < 0.7f) tile.terrain = Core::TerrainType::Grass1;
            else if (n_biome < 0.85f) tile.terrain = Core::TerrainType::Grass2;
            else tile.terrain = Core::TerrainType::Grass3;
            
            // Generate lakes and rivers
            if (n_water < config.water_density * 2.0f) {
                tile.terrain = Core::TerrainType::ShallowWater;
                if (n_water < config.water_density * 1.2f) {
                    tile.terrain = Core::TerrainType::DeepWater;
                }
                tile.is_walkable = false;
            }
            
            if (tile.is_walkable && n_forest > (1.0f - config.forest_density)) {
                tile.terrain = Core::TerrainType::Forest;
                tile.is_walkable = false;
                tile.has_tree = true;
            }
            
            // The tile's cell in the 10x10 terrain tiling pattern.
            int tens = x % 10;
            int ones = (10 - (y % 10)) % 10;
            tile.texture_variant = tens * 10 + ones;
        }
    }
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            auto& tile = grid.GetTileMut(x, y);
            if (tile.terrain == Core::TerrainType::Grass1 || tile.terrain == Core::TerrainType::Grass2 || tile.terrain == Core::TerrainType::Grass3 || tile.terrain == Core::TerrainType::Dirt1) {
                bool near_water = false;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (grid.InBounds(x + dx, y + dy)) {
                            auto t = grid.GetTile(x + dx, y + dy).terrain;
                            if (t == Core::TerrainType::ShallowWater || t == Core::TerrainType::DeepWater) {
                                near_water = true;
                                break;
                            }
                        }
                    }
                    if (near_water) break;
                }
                if (near_water) {
                    tile.terrain = Core::TerrainType::Sand;
                    tile.has_tree = false; // Clear trees from beaches
                }
            }
        }
    }

    
    std::vector<std::pair<int, int>> player_starts;
    float angle_step = 6.28318f / config.num_players;
    float start_angle = (rng() % 360) * (3.14159f / 180.0f);
    int center_x = config.width / 2;
    int center_y = config.height / 2;
    int radius = std::min(center_x, center_y) - config.base_margin;
    
    for (int p = 0; p < config.num_players; ++p) {
        float angle = start_angle + p * angle_step;
        int px = center_x + (int)(std::cos(angle) * radius);
        int py = center_y + (int)(std::sin(angle) * radius);
        
        for (int dx = -8; dx <= 8; ++dx) {
            for (int dy = -8; dy <= 8; ++dy) {
                if (grid.InBounds(px+dx, py+dy)) {
                    auto& t = grid.GetTileMut(px+dx, py+dy);
                    t.is_walkable = true;
                    t.has_tree = false;
                    if (t.terrain == Core::TerrainType::Forest) t.terrain = Core::TerrainType::Grass1;
                    if (t.terrain == Core::TerrainType::ShallowWater || t.terrain == Core::TerrainType::DeepWater) t.terrain = Core::TerrainType::Sand;
                }
            }
        }
        player_starts.push_back({px, py});
    }
    
    auto spawn_group = [&](const std::string& type, int anchor_x, int anchor_y, int count, bool tight) {
        int obj_slp = Core::ConfigManager::Get().GetSlpIdForUnitName(type);
        if (obj_slp == -1) return;
        
        for (int i = 0; i < count; ++i) {
            int ox = anchor_x, oy = anchor_y;
            bool found = false;
            
            if (!tight) {
                std::vector<std::pair<int, int>> spots;
                for (int dx = -4; dx <= 4; ++dx) {
                    for (int dy = -4; dy <= 4; ++dy) {
                        if (grid.InBounds(anchor_x+dx, anchor_y+dy) && grid.GetTile(anchor_x+dx, anchor_y+dy).is_walkable) {
                            spots.push_back({anchor_x+dx, anchor_y+dy});
                        }
                    }
                }
                if (!spots.empty()) {
                    int idx = rng() % spots.size();
                    ox = spots[idx].first; oy = spots[idx].second;
                    found = true;
                }
            } else {
                for (int r = 0; r < 6 && !found; ++r) {
                    std::vector<std::pair<int, int>> ring;
                    for (int dx = -r; dx <= r; ++dx) {
                        for (int dy = -r; dy <= r; ++dy) {
                            if (std::max(std::abs(dx), std::abs(dy)) == r) {
                                if (grid.InBounds(anchor_x+dx, anchor_y+dy) && grid.GetTile(anchor_x+dx, anchor_y+dy).is_walkable) {
                                    ring.push_back({anchor_x+dx, anchor_y+dy});
                                }
                            }
                        }
                    }
                    if (!ring.empty()) {
                        int idx = rng() % ring.size();
                        ox = ring[idx].first; oy = ring[idx].second;
                        found = true;
                    }
                }
            }
            
            if (found) {
                grid.GetTileMut(ox, oy).is_walkable = false;
                auto entity = registry.create();
                registry.emplace<ECS::Components::Transform>(entity, ECS::Components::Transform{
                    .position = { Core::Fixed32(ox), Core::Fixed32(oy) },
                    .previous_position = { Core::Fixed32(ox), Core::Fixed32(oy) }
                });
                
                int frame = 0;
                if (type == "GOLD") frame = 5;
                else if (type == "STONE") frame = 3;
                
                registry.emplace<ECS::Components::Sprite>(entity, ECS::Components::Sprite{
                    .slp_id = obj_slp,
                    .slp_frame = frame,
                    .width = 0, .height = 0,
                });
            }
        }
    };
    
    for (const auto& start : player_starts) {
        for (int i=0; i<config.gold_mines; ++i) {
            int dist = 10 + (rng() % 15); float ang = (rng() % 360) * (3.14159f / 180.0f);
            spawn_group("GOLD", start.first + std::cos(ang)*dist, start.second + std::sin(ang)*dist, 7, true);
        }
        for (int i=0; i<config.stone_mines; ++i) {
            int dist = 12 + (rng() % 15); float ang = (rng() % 360) * (3.14159f / 180.0f);
            spawn_group("STONE", start.first + std::cos(ang)*dist, start.second + std::sin(ang)*dist, 5, true);
        }
        for (int i=0; i<config.forage_bushes; ++i) {
            int dist = 8 + (rng() % 5); float ang = (rng() % 360) * (3.14159f / 180.0f);
            spawn_group("FORAGE", start.first + std::cos(ang)*dist, start.second + std::sin(ang)*dist, 6, true);
        }
        for (int i=0; i<config.sheep_flocks; ++i) {
            int dist = 9 + (rng() % 8); float ang = (rng() % 360) * (3.14159f / 180.0f);
            spawn_group("SHEEP", start.first + std::cos(ang)*dist, start.second + std::sin(ang)*dist, 4, false);
        }
        for (int i=0; i<config.boars; ++i) {
            int dist = 14 + (rng() % 6); float ang = (rng() % 360) * (3.14159f / 180.0f);
            spawn_group("BOAR", start.first + std::cos(ang)*dist, start.second + std::sin(ang)*dist, 1, false);
        }
        for (int i=0; i<config.deer_herds; ++i) {
            int dist = 18 + (rng() % 10); float ang = (rng() % 360) * (3.14159f / 180.0f);
            spawn_group("DEER", start.first + std::cos(ang)*dist, start.second + std::sin(ang)*dist, 4, false);
        }
    }
    
    for (int i=0; i<config.total_relics; ++i) {
        int rx = rng() % config.width;
        int ry = rng() % config.height;
        spawn_group("RELIC", rx, ry, 1, false);
    }
    
    int tree_slp = Core::ConfigManager::Get().GetSlpIdForUnitName("OAKTREE");
    if (tree_slp == -1) tree_slp = 4652;
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            if (grid.GetTile(x, y).has_tree) {
                auto entity = registry.create();
                registry.emplace<ECS::Components::Transform>(entity, ECS::Components::Transform{
                    .position = { Core::Fixed32(x), Core::Fixed32(y) },
                    .previous_position = { Core::Fixed32(x), Core::Fixed32(y) }
                });
                registry.emplace<ECS::Components::Sprite>(entity, ECS::Components::Sprite{
                    .slp_id = tree_slp,
                    .slp_frame = static_cast<int>(rng() % 14), // Tree shape variant
                    .width = 0, .height = 0,
                });
            }
        }
    }
    
    return player_starts;
}
