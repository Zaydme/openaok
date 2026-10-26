#include "verify/Harness.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ai/Doctrine.h"
#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;

Vector2D At(int x, int y) { return {Fixed32(x), Fixed32(y)}; }

// The first tile out from (x, y) along `dx` that is open sea with open sea
// for `depth` tiles beyond it, so a ship set there is well off the shore.
bool OutToSea(const Physics::Grid& grid, Physics::Layer sea, int x, int y, int dx,
              int depth, Vector2D& out) {
    for (int step = 1; step < grid.GetWidth(); ++step) {
        const int tx = x + dx * step;
        if (!grid.InBounds(tx, y)) return false;
        if (grid.IsWalkable(tx, y) || !grid.Passable(sea, tx, y)) continue;
        bool open = true;
        for (int d = 1; d <= depth && open; ++d) open = grid.Passable(sea, tx + dx * d, y);
        if (!open) continue;
        out = At(tx + dx * depth, y);
        return true;
    }
    return false;
}

// Whether the straight line between two points crosses ground the sea's
// movers cannot cross -- that is, whether a ship going straight would run
// aground.
bool CrossesLand(const Physics::Grid& grid, Physics::Layer sea, Vector2D a, Vector2D b) {
    const int steps = 200;
    for (int i = 0; i <= steps; ++i) {
        const Fixed32 t = Fixed32(i) / Fixed32(steps);
        const Fixed32 x = a.x + (b.x - a.x) * t;
        const Fixed32 y = a.y + (b.y - a.y) * t;
        if (!grid.Passable(sea, Physics::TileOf(x), Physics::TileOf(y))) return true;
    }
    return false;
}

} // namespace

// Water as ground of its own, on Islands. The sea is a passability layer of
// its own with its own regions; beach and shallows belong to land and sea
// alike; a galley sent from one side of its player's island to the other sails
// round it -- never once on ground a ship cannot cross -- while a villager
// given the same order is refused, because no walk joins an island to the sea.
bool CheckWater() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();

    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Islands";
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();

    // The layers, from the dat: every ship's restriction moves on one sea --
    // a fishing ship's, a galley's, a transport's -- and it is not land.
    const int galley = cfg.GetUnitIdByName("SGALY");
    const int fishing = cfg.GetUnitIdByName("FSHSP");
    const int transport = cfg.GetUnitIdByName("XPORT");
    const int villager = cfg.GetUnitIdByName("VMBAS");
    const Core::Rules::UnitRules* galley_rules = Core::Rules::Unit(galley);
    const Core::Rules::UnitRules* fishing_rules = Core::Rules::Unit(fishing);
    const Core::Rules::UnitRules* transport_rules = Core::Rules::Unit(transport);
    Physics::Layer sea = Physics::kLand;
    if (galley_rules == nullptr || fishing_rules == nullptr || transport_rules == nullptr) {
        verdict = "the dat has no ships";
    } else {
        sea = grid.LayerOf(galley_rules->terrain_restriction);
        if (sea == Physics::kLand) {
            verdict = "ships move on land's layer";
        } else if (grid.LayerOf(fishing_rules->terrain_restriction) != sea ||
                   grid.LayerOf(transport_rules->terrain_restriction) != sea) {
            verdict = "ships with the same grounds were given different layers";
        } else if (ECS::Navigation::LayerOfUnit(reg, grid, 1, villager) != Physics::kLand) {
            verdict = "a villager does not move on land";
        }
    }

    // The ground: water is the sea's and not land's, grass land's and not the
    // sea's, and beach and shallows both -- tile by tile, by the dat's own
    // restrictions, over the whole map.
    int water = 0, grass = 0, shared = 0;
    for (int y = 0; y < grid.GetHeight() && ok(); ++y) {
        for (int x = 0; x < grid.GetWidth() && ok(); ++x) {
            const auto& tile = grid.GetTile(x, y);
            if (tile.blocked) {
                if (grid.Passable(sea, x, y)) verdict = "a ship may sail through something standing";
                continue;
            }
            const int dat = cfg.GetTerrainArt(tile.terrain).dat_id;
            const bool land_ok = Core::Rules::Passable(villager >= 0 ? Core::Rules::Unit(villager)
                                                           ->terrain_restriction : 7, dat);
            const bool sea_ok = Core::Rules::Passable(galley_rules->terrain_restriction, dat);
            if (grid.IsWalkable(x, y) != land_ok) verdict = "land's layer disagrees with the dat";
            if (grid.Passable(sea, x, y) != sea_ok) verdict = "the sea's layer disagrees with the dat";
            if (sea_ok && !land_ok) ++water;
            if (land_ok && !sea_ok) ++grass;
            if (land_ok && sea_ok) ++shared;
        }
    }
    if (ok() && (water == 0 || grass == 0)) verdict = "Islands has no sea or no land";
    if (ok() && shared == 0) verdict = "no ground is both land's and the sea's";

    // Regions per layer: the two islands are apart on land, and one sea joins
    // the water off both.
    const auto& starts = sim.player_starts;
    Vector2D west{}, east{}, far_side{};
    if (ok() && starts.size() < 2) verdict = "Islands placed fewer than two players";
    if (ok()) {
        const auto [sx, sy] = starts[0];
        if (!OutToSea(grid, sea, sx, sy, -1, 2, west) || !OutToSea(grid, sea, sx, sy, 1, 2, east)) {
            verdict = "no sea either side of the first island";
        } else if (!OutToSea(grid, sea, starts[1].first, starts[1].second, 1, 2, far_side)) {
            verdict = "no sea beside the second island";
        } else if (!CrossesLand(grid, sea, west, east)) {
            verdict = "the way across the island is open water";
        }
    }
    if (ok()) {
        auto region = [&](Vector2D p, Physics::Layer layer) {
            return Pathfinding::RegionAt(grid, Physics::TileOf(p.x), Physics::TileOf(p.y), layer);
        };
        const int island_a = Pathfinding::RegionAt(grid, starts[0].first + 3, starts[0].second + 3);
        const int island_b = Pathfinding::RegionAt(grid, starts[1].first + 3, starts[1].second + 3);
        if (region(west, sea) < 0 || region(west, sea) != region(east, sea) ||
            region(west, sea) != region(far_side, sea)) {
            verdict = "the sea round the islands is not one region";
        } else if (island_a < 0 || island_b < 0 || island_a == island_b) {
            verdict = "the islands are one region on land";
        } else if (region(west, Physics::kLand) >= 0) {
            verdict = "open sea has a land region";
        }
    }

    // A galley round the island, from west of it to east of it.
    entt::entity ship = entt::null;
    if (ok()) {
        ship = ECS::Units::Spawn(reg, galley, 1, west);
        if (ship == entt::null) verdict = "a galley could not be made";
    }
    if (ok()) {
        Core::CommandQueue::Get().PushCommand(
            Core::MoveCommand{ship, east.x, east.y, Core::PlayerId{1}});
        const Fixed32 straight = Core::FixedMath::Distance(east.x - west.x, east.y - west.y);
        Fixed32 sailed(0);
        Vector2D last = west;
        bool arrived = false;
        bool routed = false;
        for (int t = 0; t < 9000 && ok(); ++t) {
            sim.Update(16);
            if (!reg.valid(ship) || !reg.all_of<ECS::Components::Transform>(ship)) {
                verdict = "the galley was lost on the way";
                break;
            }
            const Vector2D now = reg.get<ECS::Components::Transform>(ship).position;
            if (!grid.Passable(sea, Physics::TileOf(now.x), Physics::TileOf(now.y))) {
                verdict = "the galley sailed onto ground a ship cannot cross";
                break;
            }
            sailed = sailed + Core::FixedMath::Distance(now.x - last.x, now.y - last.y);
            last = now;
            const auto* path = reg.try_get<ECS::Components::Path>(ship);
            if (path != nullptr && !path->waypoints.empty()) routed = true;
            if (Core::FixedMath::Distance(now.x - east.x, now.y - east.y) <
                    Fixed32(1) / Fixed32(2) &&
                (path == nullptr || path->current_waypoint_index >= path->waypoints.size())) {
                arrived = true;
                break;
            }
        }
        if (ok() && !routed) verdict = "the galley was given no route";
        if (ok() && !arrived) verdict = "the galley never reached the far side of the island";
        // Round, not through: the sail is longer than the line across.
        if (ok() && sailed <= straight + Fixed32(2)) verdict = "the galley went straight across";
    }

    // A villager sent to the same water is refused: there is no walk to it.
    if (ok()) {
        entt::entity walker = entt::null;
        auto villagers = reg.view<ECS::Components::Worker, ECS::Components::Owner,
                                  ECS::Components::Transform>();
        for (auto v : villagers) {
            if (villagers.get<ECS::Components::Owner>(v).player_id == 1) walker = v;
        }
        if (walker == entt::null) {
            verdict = "no villager";
        } else {
            const Vector2D from = reg.get<ECS::Components::Transform>(walker).position;
            if (!ECS::Navigation::Route(reg, grid, walker, from, east).waypoints.empty()) {
                verdict = "a villager was given a route out to sea";
            } else if (ECS::Navigation::Route(reg, grid, ship, east, from).waypoints.size() > 0 &&
                       !grid.Passable(sea, Physics::TileOf(from.x), Physics::TileOf(from.y))) {
                verdict = "a ship was given a route inland";
            }
        }
    }

    // Sent at the land -- a few tiles in from the island's far shore -- a ship
    // sails round to the shore nearest the point and stops there, on the
    // water.
    if (ok()) {
        // Ground, not what stands on it: the Town Center is in the way.
        int edge = starts[0].first;
        while (edge > 0 && grid.LandPassable(grid.GetTile(edge - 1, starts[0].second).terrain)) --edge;
        const Vector2D inland = At(edge + 3, starts[0].second);
        const Vector2D before = reg.get<ECS::Components::Transform>(ship).position;
        Core::CommandQueue::Get().PushCommand(
            Core::MoveCommand{ship, inland.x, inland.y, Core::PlayerId{1}});
        for (int t = 0; t < 90 * 1000 / 16; ++t) sim.Update(16);
        const Vector2D now = reg.get<ECS::Components::Transform>(ship).position;
        auto apart = [](Vector2D a, Vector2D b) {
            return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
        };
        bool ashore = false;
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                ashore = ashore || grid.IsWalkable(Physics::TileOf(now.x) + ox, Physics::TileOf(now.y) + oy);
            }
        }
        if (!grid.Passable(sea, Physics::TileOf(now.x), Physics::TileOf(now.y))) {
            verdict = "a ship sent at the land went onto it";
        } else if (apart(now, inland) + Fixed32(5) > apart(before, inland) || !ashore) {
            verdict = "a ship sent at the land did not come in to the shore";
        }
    }

    // Every map the AI keeps a fleet on generates and plays at sea: each player
    // has a shore of its own land that takes a Dock, and a fishing ship trained
    // there is launched on the water with fish it can reach.
    if (ok()) {
        const int dock_id = cfg.GetUnitIdByName("DOCK");
        const int fishing_id = cfg.GetUnitIdByName("FSHSP");
        int maps = 0;
        for (const char* script : {"Archipelago", "Baltic", "Coastal", "Continental", "CraterLake",
                                   "Islands", "Mediterranean", "Migration", "Rivers",
                                   "TeamIslands"}) {
            if (!ok()) break;
            if (!AI::Doctrine::FishingMap(script)) continue;
            ++maps;
            Core::GameSetup game = Core::DefaultSetup();
            game.map_script = script;
            ECS::Simulation world;
            world.Init(game);
            auto& wreg = world.GetRegistry();
            auto& wgrid = world.GetGrid();
            if (world.player_starts.size() < 2) {
                verdict = "a water map placed fewer than two players";
                break;
            }
            for (size_t s = 0; s < world.player_starts.size() && ok(); ++s) {
                const Core::PlayerId player = static_cast<Core::PlayerId>(s + 1);
                const auto [px, py] = world.player_starts[s];
                const int land = Pathfinding::RegionAt(wgrid, px + 3, py + 3);
                // The nearest Dock spot by the dat's rules that touches this
                // player's own land and a body of water the size the AI asks
                // for -- a river's backwater may have no fish in it.
                const Physics::Layer wsea = Shore::Sea(world);
                entt::entity dock = entt::null;
                for (int r = 0; r <= 50 && dock == entt::null; ++r) {
                    for (int dy = -r; dy <= r && dock == entt::null; ++dy) {
                        for (int dx = -r; dx <= r && dock == entt::null; ++dx) {
                            if (std::abs(dx) != r && std::abs(dy) != r) continue;
                            const int x = px + dx, y = py + dy;
                            if (!ECS::Buildings::CanPlace(wgrid, dock_id, x, y)) continue;
                            bool ours = false;
                            bool big = false;
                            for (int oy = -2; oy <= 2; ++oy) {
                                for (int ox = -2; ox <= 2; ++ox) {
                                    ours = ours || Pathfinding::RegionAt(wgrid, x + ox, y + oy) == land;
                                    const int water = Pathfinding::RegionAt(wgrid, x + ox, y + oy, wsea);
                                    big = big || (water >= 0 && Pathfinding::RegionSize(wgrid, water, wsea) >=
                                                                    AI::Doctrine::DockWaterTiles());
                                }
                            }
                            if (ours && big) dock = ECS::Buildings::Place(wreg, wgrid, dock_id, player, x, y, true);
                        }
                    }
                }
                if (dock == entt::null) {
                    verdict = "a player on a water map has no shore for a dock";
                    break;
                }
                std::vector<entt::entity> before;
                auto boats = wreg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
                for (auto b : boats) {
                    if (boats.get<const ECS::Components::Identity>(b).unit_id == fishing_id) before.push_back(b);
                }
                auto* seat = ECS::Players::Find(wreg, player);
                ECS::Players::AddStock(*seat, Core::ResourceType::Wood, 100);
                Core::CommandQueue::Get().PushCommand(Core::TrainCommand{dock, fishing_id, player});
                entt::entity boat = entt::null;
                for (int t = 0; t < 60 * 1000 / 16 && boat == entt::null; ++t) {
                    world.Update(16);
                    auto now = wreg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
                    for (auto b : now) {
                        if (now.get<const ECS::Components::Identity>(b).unit_id != fishing_id) continue;
                        if (std::find(before.begin(), before.end(), b) == before.end()) boat = b;
                    }
                }
                if (boat == entt::null) {
                    verdict = "a dock on a water map never launched its fishing ship";
                    break;
                }
                const Vector2D at = wreg.get<ECS::Components::Transform>(boat).position;
                if (!wgrid.Passable(wsea, Physics::TileOf(at.x), Physics::TileOf(at.y))) {
                    verdict = "a fishing ship on a water map was launched onto the land";
                } else if (Shore::NearestOfClass(world, boat, 5, at) == entt::null) {
                    verdict = "a fishing ship on a water map has no fish it can reach";
                }
            }
        }
        if (ok() && maps < 5) verdict = "fewer water maps than the original's AI names";
    }

    std::printf("water=%s\n", verdict);
    return ok();
}

} // namespace Verify
