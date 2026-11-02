#include "core/Connections.h"

#include <algorithm>
#include <array>
#include <functional>
#include <queue>

#include "core/ConfigManager.h"
#include "core/MapGenerator.h"
#include "core/Random.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"

namespace Core::Connections {

namespace {

// Room for every terrain id the game has (TerrainType is a byte).
constexpr size_t kTerrains = 256;
// A terrain the script names no cost or size for.
constexpr int kDefaultCost = 1;
constexpr int kDefaultRadius = 1;

// One create_connect_* command's figures, by terrain.
struct Figures {
    std::array<int, kTerrains> cost;
    std::array<int, kTerrains> radius;
    std::array<int, kTerrains> variance;
    std::array<int, kTerrains> replace;   // the terrain it becomes, -1 for none
};

size_t Index(TerrainType t) { return static_cast<size_t>(t); }

Figures Read(const RMSCommand& command) {
    Figures f;
    f.cost.fill(kDefaultCost);
    f.radius.fill(kDefaultRadius);
    f.variance.fill(0);
    f.replace.fill(-1);
    const auto& cfg = ConfigManager::Get();
    int everything = -1;
    for (const RMSCommand& a : command.attributes) {
        TerrainType t{};
        if (a.name == "default_terrain_replacement") {
            if (cfg.TerrainForRmsId(a.Value(0, -1), t)) everything = static_cast<int>(t);
            continue;
        }
        if (!cfg.TerrainForRmsId(a.Value(0, -1), t)) continue;
        if (a.name == "terrain_cost") {
            f.cost[Index(t)] = a.Value(1, kDefaultCost);
        } else if (a.name == "terrain_size") {
            f.radius[Index(t)] = std::max(0, a.Value(1, kDefaultRadius));
            f.variance[Index(t)] = std::max(0, a.Value(2, 0));
        } else if (a.name == "replace_terrain") {
            TerrainType to{};
            if (cfg.TerrainForRmsId(a.Value(1, -1), to)) f.replace[Index(t)] = static_cast<int>(to);
        }
    }
    if (everything >= 0) {
        for (int& r : f.replace) {
            if (r < 0) r = everything;
        }
    }
    return f;
}

// The cheapest way from `from` to `to`, eight ways a step, each tile entered
// costing its terrain's figure; the tiles in order from `from`, or none when
// there is no way. Ties go to the lower tile index, so the way is the same on
// every machine.
std::vector<int> Way(const Physics::Grid& grid, const Figures& f, std::pair<int, int> from, std::pair<int, int> to) {
    const int w = grid.GetWidth(), h = grid.GetHeight();
    if (!grid.InBounds(from.first, from.second) || !grid.InBounds(to.first, to.second)) return {};
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    static std::vector<int64_t> spent;
    static std::vector<int> back;
    spent.assign(n, INT64_MAX);
    back.assign(n, -1);
    using Open = std::pair<int64_t, int>;
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    const int start = from.second * w + from.first;
    const int goal = to.second * w + to.first;
    spent[static_cast<size_t>(start)] = 0;
    open.push({0, start});
    static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        const auto [cost, at] = open.top();
        open.pop();
        if (cost != spent[static_cast<size_t>(at)]) continue;
        if (at == goal) break;
        const int x = at % w, y = at / w;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + kDx[k], ny = y + kDy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const int step = f.cost[Index(grid.GetTile(nx, ny).terrain)];
            if (step <= 0) continue;
            const int next = ny * w + nx;
            if (cost + step < spent[static_cast<size_t>(next)]) {
                spent[static_cast<size_t>(next)] = cost + step;
                back[static_cast<size_t>(next)] = at;
                open.push({cost + step, next});
            }
        }
    }
    if (spent[static_cast<size_t>(goal)] == INT64_MAX) return {};
    std::vector<int> way;
    for (int at = goal; at >= 0; at = back[static_cast<size_t>(at)]) way.push_back(at);
    std::reverse(way.begin(), way.end());
    return way;
}

// Lays one connection along `way`: round each of its tiles, as far as the
// terrain there asks, the ground the script replaces is replaced. Tiles that
// lost their tree are marked in `cleared`.
void Lay(Physics::Grid& grid, const Figures& f, const std::vector<int>& way, Random& rng,
         std::vector<uint8_t>& cleared) {
    const int w = grid.GetWidth();
    const auto& cfg = ConfigManager::Get();
    // Each tile's reach from its terrain before any of the way is laid, so a
    // forest cut back by the tile before keeps its own width.
    static std::vector<int> reach;
    reach.clear();
    for (int at : way) {
        const size_t t = Index(grid.GetTile(at % w, at / w).terrain);
        const int v = f.variance[t];
        reach.push_back(std::max(0, f.radius[t] + (v > 0 ? rng.NextRange(-v, v) : 0)));
    }
    for (size_t i = 0; i < way.size(); ++i) {
        const int cx = way[i] % w, cy = way[i] / w, r = reach[i];
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r) continue;
                const int x = cx + dx, y = cy + dy;
                if (!grid.InBounds(x, y)) continue;
                Physics::Tile& tile = grid.GetTileMut(x, y);
                const int to = f.replace[Index(tile.terrain)];
                if (to < 0 || to == static_cast<int>(tile.terrain)) continue;
                const auto terrain = static_cast<TerrainType>(to);
                if (tile.has_tree && cfg.GetTerrainArt(terrain).tree_unit_id < 0) {
                    tile.has_tree = false;
                    grid.SetWalkable(x, y, true);
                    cleared[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 1;
                }
                MapGenerator::Paint(grid, x, y, terrain);
            }
        }
    }
}

} // namespace

void Generate(Physics::Grid& grid, entt::registry& registry, const RMSSection& section,
              const std::vector<std::pair<int, int>>& starts, const std::vector<TeamId>& teams, uint32_t seed) {
    const int w = grid.GetWidth();
    Random rng((static_cast<uint64_t>(seed) ^ 0xC0EC7105ull) | 1u);
    std::vector<uint8_t> cleared(static_cast<size_t>(w) * static_cast<size_t>(grid.GetHeight()), 0);
    for (const RMSCommand& command : section.commands) {
        const bool all = command.name == "create_connect_all_players_land";
        const bool team = command.name == "create_connect_teams_lands";
        if (!all && !team) continue;
        const Figures f = Read(command);
        for (size_t a = 0; a < starts.size(); ++a) {
            for (size_t b = a + 1; b < starts.size(); ++b) {
                if (team) {
                    const int ta = a < teams.size() ? static_cast<int>(teams[a]) : 0;
                    const int tb = b < teams.size() ? static_cast<int>(teams[b]) : 0;
                    if (ta == 0 || ta != tb) continue;
                }
                Lay(grid, f, Way(grid, f, starts[a], starts[b]), rng, cleared);
            }
        }
    }
    // The trees of the ground cut back.
    std::vector<entt::entity> felled;
    auto trees = registry.view<ECS::Components::Transform, ECS::Components::Resource>();
    for (auto e : trees) {
        const auto& at = trees.get<ECS::Components::Transform>(e).position;
        const int x = at.x.ToInt(), y = at.y.ToInt();
        if (!grid.InBounds(x, y)) continue;
        if (cleared[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]) felled.push_back(e);
    }
    for (entt::entity e : felled) registry.destroy(e);
}

} // namespace Core::Connections
