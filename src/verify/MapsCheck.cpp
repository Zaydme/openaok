#include "verify/Harness.h"
#include "ecs/components/Building.h"
#include "ecs/components/Owner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/Cliffs.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace Verify {

// Cliffs laid by core/Cliffs' table: every piece one of the table's, standing
// exactly at its place in its 3x3 cell, and every side it links on meeting the
// piece of the next cell, three tiles on, linking back the same way -- face to
// face, lip to lip. A seam is a link that meets nothing, or a piece off its
// place. The cliff frame is turned onto the grid as the dat's own offsets are
// (ecs/BuildingGeometry's FromDatAxes); in any other frame the art does not
// join however the pieces are chosen.
const char* CliffsJoin(ECS::Simulation& sim) {
    using Core::Fixed32;
    namespace Cliffs = Core::Cliffs;
    auto& grid = sim.GetGrid();
    auto& reg = sim.GetRegistry();

    const auto o = Cliffs::FromOriginalHalves(grid, 10, 10);
    const auto along_x = Cliffs::FromOriginalHalves(grid, 12, 10);
    const auto along_y = Cliffs::FromOriginalHalves(grid, 10, 12);
    const auto dat_x = ECS::Buildings::FromDatAxes(Fixed32(1), Fixed32(0));
    const auto dat_y = ECS::Buildings::FromDatAxes(Fixed32(0), Fixed32(1));
    if (along_x.first - o.first != dat_x.x || along_x.second - o.second != dat_x.y ||
        along_y.first - o.first != dat_y.x || along_y.second - o.second != dat_y.y ||
        Cliffs::OriginalHalves(grid, o.first, o.second) != std::pair<int, int>(10, 10)) {
        return "cliffs are laid out in a frame other than the dat's";
    }

    constexpr int kCliffClass = 34;
    std::map<std::pair<int, int>, const Cliffs::Piece*> cells;
    auto all = reg.view<const ECS::Components::Identity, const ECS::Components::Transform,
                        const ECS::Components::Sprite>();
    for (auto e : all) {
        const int unit = all.get<const ECS::Components::Identity>(e).unit_id;
        const auto* rules = Core::Rules::Unit(unit);
        if (rules == nullptr || rules->cls != kCliffClass) continue;
        const Cliffs::Piece* piece = Cliffs::PieceDrawn(unit, all.get<const ECS::Components::Sprite>(e).slp_frame);
        if (piece == nullptr) return "a cliff piece is none of the original's";
        const auto& p = all.get<const ECS::Components::Transform>(e).position;
        const auto [x2, y2] = Cliffs::OriginalHalves(grid, p.x, p.y);
        const int cx = x2 / 6;
        const int cy = y2 / 6;
        if (x2 < 0 || y2 < 0 || x2 - 6 * cx != piece->x2 || y2 - 6 * cy != piece->y2) {
            return "a cliff piece stands off its place in its cell";
        }
        if (!cells.emplace(std::pair<int, int>(cx, cy), piece).second) return "two cliff pieces share a cell";
    }
    // Links run +x, +y, -x, -y in the cliff frame.
    static constexpr int kDx[4] = {1, 0, -1, 0};
    static constexpr int kDy[4] = {0, 1, 0, -1};
    for (const auto& [cell, piece] : cells) {
        for (size_t d = 0; d < 4; ++d) {
            const int value = piece->links[d];
            if (value == 0) continue;
            const auto next = cells.find({cell.first + kDx[d], cell.second + kDy[d]});
            if (next == cells.end() || next->second->links[(d + 2) % 4] != value) {
                return "a cliff's pieces do not join";
            }
        }
    }
    return nullptr;
}

namespace {

// Every tile a land unit can reach from (x, y), walking only.
std::vector<uint8_t> Reach(const Physics::Grid& grid, int x, int y) {
    const int w = grid.GetWidth();
    std::vector<uint8_t> seen(static_cast<size_t>(w) * static_cast<size_t>(grid.GetHeight()), 0);
    std::vector<std::pair<int, int>> open{{x, y}};
    // A start's own tile holds its Town Center; begin from the ground round it.
    seen[static_cast<size_t>(y * w + x)] = 1;
    while (!open.empty()) {
        const auto [cx, cy] = open.back();
        open.pop_back();
        static constexpr int kDx[4] = {1, -1, 0, 0};
        static constexpr int kDy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + kDx[k];
            const int ny = cy + kDy[k];
            if (!grid.InBounds(nx, ny) || seen[static_cast<size_t>(ny * w + nx)]) continue;
            // Land, whatever stands on it: a wood is in the way, not the sea.
            if (!grid.LandPassable(grid.GetTile(nx, ny).terrain)) continue;
            seen[static_cast<size_t>(ny * w + nx)] = 1;
            open.emplace_back(nx, ny);
        }
    }
    return seen;
}

bool Connected(const ECS::Simulation& sim) {
    const auto& grid = const_cast<ECS::Simulation&>(sim).GetGrid();
    const auto& starts = sim.player_starts;
    if (starts.size() < 2) return true;
    const auto reach = Reach(grid, starts[0].first, starts[0].second);
    const auto& other = starts[1];
    return reach[static_cast<size_t>(other.second * grid.GetWidth() + other.first)] != 0;
}

// A random map's cliffs: they join (CliffsJoin), there are some where the
// script always makes some, and each stands on ground it blocks, clear of
// the starts and of the water. Null when they hold.
const char* CheckCliffs(ECS::Simulation& sim, bool required) {
    if (const char* joined = CliffsJoin(sim)) return joined;
    constexpr int kCliffClass = 34;
    auto& grid = sim.GetGrid();
    auto& reg = sim.GetRegistry();
    int cliffs = 0;
    auto all = reg.view<const ECS::Components::Identity, const ECS::Components::Transform>();
    for (auto e : all) {
        const auto* rules = Core::Rules::Unit(all.get<const ECS::Components::Identity>(e).unit_id);
        if (rules == nullptr || rules->cls != kCliffClass) continue;
        ++cliffs;
        const auto& p = all.get<const ECS::Components::Transform>(e).position;
        const int x = Physics::TileOf(p.x);
        const int y = Physics::TileOf(p.y);
        if (!grid.IsBlocked(x, y)) return "a cliff piece did not block its ground";
        for (const auto& start : sim.player_starts) {
            const int dx = x - start.first;
            const int dy = y - start.second;
            if (dx * dx + dy * dy < 8 * 8) return "a cliff stood in a player's start";
        }
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (grid.InBounds(x + ox, y + oy) && !grid.LandPassable(grid.GetTile(x + ox, y + oy).terrain)) {
                    return "a cliff stood in the water";
                }
            }
        }
    }
    if (required && cliffs == 0) return "a map that asks for cliffs had none";
    return nullptr;
}

} // namespace

// The land pass of the random map scripts, judged by what the player would
// notice: islands that are islands, continents that are one, a beach along
// every shore, every start on its own ground, and fish in the water.
bool CheckMaps() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    // The dat's classes for fish.
    constexpr int kOceanFish = 5;
    constexpr int kShoreFish = 33;

    struct Case {
        const char* script;
        bool connected;   // whether the two players share a landmass
        bool water;       // whether the map has shores to judge
    };
    static const Case kCases[] = {
        {"Arabia", true, false},
        {"Islands", false, true},
        {"TeamIslands", false, true},
        {"Baltic", true, true},
        {"Coastal", true, true},
    };

    for (const Case& c : kCases) {
        if (!ok()) break;
        Core::GameSetup setup = Core::DefaultSetup();
        setup.map_script = c.script;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& grid = sim.GetGrid();
        auto& reg = sim.GetRegistry();

        for (const auto& start : sim.player_starts) {
            if (!grid.InBounds(start.first, start.second) ||
                !grid.LandPassable(grid.GetTile(start.first, start.second).terrain)) {
                verdict = "a player started off land";
            }
        }
        if (!ok()) break;

        // Cliffs, on every map: where the script asks for them (Arabia always
        // has some), pieces that join, standing on ground they block, clear of
        // the starts and of the water.
        if (const char* cliffs = CheckCliffs(sim, std::strcmp(c.script, "Arabia") == 0)) {
            verdict = cliffs;
            break;
        }

        if (Connected(sim) != c.connected) {
            verdict = c.connected ? "players meant to share a landmass were cut off"
                                  : "players meant to be on islands could walk to each other";
            break;
        }
        // The objects the scripts place, by their own rules: never two on one
        // tile; and on Arabia each player's eight sheep -- four by the Town
        // Center and two groups of two further out (number_of_groups groups
        // each of number_of_objects).
        {
            std::vector<int> tiles;
            int sheep = 0;
            const int sheep_id = Core::ConfigManager::Get().GetUnitIdByName("SHEEPG");
            auto placed = reg.view<const ECS::Components::Identity, const ECS::Components::Transform>(
                entt::exclude<ECS::Components::Building>);
            for (auto e : placed) {
                // The map's, or a sheep a player has already claimed.
                const bool is_sheep = placed.get<const ECS::Components::Identity>(e).unit_id == sheep_id;
                if (const auto* owner = reg.try_get<ECS::Components::Owner>(e)) {
                    if (owner->player_id != Core::PLAYER_GAIA && !is_sheep) continue;
                }
                const auto& p = placed.get<const ECS::Components::Transform>(e).position;
                tiles.push_back(p.y.ToInt() * grid.GetWidth() + p.x.ToInt());
                if (is_sheep) ++sheep;
            }
            std::sort(tiles.begin(), tiles.end());
            if (std::adjacent_find(tiles.begin(), tiles.end()) != tiles.end()) {
                verdict = "two of the map's objects were placed on one tile";
                break;
            }
            if (std::strcmp(c.script, "Arabia") == 0 &&
                sheep != 8 * static_cast<int>(sim.player_starts.size())) {
                std::printf("maps: %d sheep for %zu players\n", sheep, sim.player_starts.size());
                verdict = "Arabia's players were not each given their eight sheep";
                break;
            }
        }
        if (!c.water) continue;

        // Every land tile at the water's edge is beach, or forest to the
        // waterline -- never grass running straight into the sea.
        int bare = 0;
        int shore = 0;
        for (int y = 0; y < grid.GetHeight(); ++y) {
            for (int x = 0; x < grid.GetWidth(); ++x) {
                const auto& tile = grid.GetTile(x, y);
                if (!grid.LandPassable(tile.terrain)) continue;
                bool wet = false;
                for (int oy = -1; oy <= 1 && !wet; ++oy) {
                    for (int ox = -1; ox <= 1 && !wet; ++ox) {
                        if (grid.InBounds(x + ox, y + oy) &&
                            !grid.LandPassable(grid.GetTile(x + ox, y + oy).terrain)) {
                            wet = true;
                        }
                    }
                }
                if (!wet) continue;
                ++shore;
                if (tile.terrain != Core::TerrainType::Beach && !tile.has_tree) ++bare;
            }
        }
        if (shore == 0) {
            verdict = "a water map had no shore";
        } else if (bare > 0) {
            verdict = "a shore ran into the water without a beach";
        }
        if (!ok()) break;

        // Fish -- every kind the script asks for: ocean fish, shore fish --
        // are in the water, and there are some.
        int fish = 0;
        auto things = reg.view<const ECS::Components::Identity, const ECS::Components::Transform>();
        for (auto e : things) {
            const auto* rules = Core::Rules::Unit(things.get<const ECS::Components::Identity>(e).unit_id);
            if (rules == nullptr || (rules->cls != kOceanFish && rules->cls != kShoreFish)) continue;
            ++fish;
            // Where its own terrain restriction lets it be: most fish only in
            // water, a dolphin also in the shallows and off the beach.
            const auto& p = things.get<const ECS::Components::Transform>(e).position;
            const int ground = Core::ConfigManager::Get()
                .GetTerrainArt(grid.GetTile(p.x.ToInt(), p.y.ToInt()).terrain).dat_id;
            if (!Core::Rules::Passable(rules->terrain_restriction, ground)) {
                verdict = "a fish was placed where it cannot be";
                break;
            }
        }
        if (ok() && fish == 0) verdict = "a water map had no fish";
    }

    std::printf("maps=%s\n", verdict);
    return ok();
}

} // namespace Verify
