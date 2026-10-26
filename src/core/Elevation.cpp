#include "core/Elevation.h"

#include <algorithm>
#include <deque>

#include "core/ConfigManager.h"
#include "core/Random.h"

namespace Core::Elevation {

namespace {

// Offsets the hill stream from the map's own, so drawing from one never
// shifts the other.
constexpr uint64_t kStreamSalt = 0x45C3B1E7A95D2F13ull;

} // namespace

void Generate(Physics::Grid& grid, const RMSScript& script, uint32_t seed,
              const std::vector<std::pair<int, int>>& starts, int start_radius) {
    const RMSSection* section = script.GetSection("ELEVATION_GENERATION");
    if (section == nullptr) return;
    auto& cfg = ConfigManager::Get();
    Random rng((static_cast<uint64_t>(seed) ^ kStreamSalt) | 1);

    const int width = grid.GetWidth();
    const int height = grid.GetHeight();
    const int tiles = width * height;
    auto index = [width](int x, int y) { return static_cast<size_t>(y * width + x); };
    auto near_start = [&](int x, int y) {
        for (const auto& start : starts) {
            const int dx = x - start.first;
            const int dy = y - start.second;
            if (dx * dx + dy * dy < start_radius * start_radius) return true;
        }
        return false;
    };

    for (const RMSCommand& command : section->commands) {
        if (command.name != "create_elevation") continue;
        const int top = std::max(1, command.Value(0, 1));
        TerrainType base = TerrainType::Grass1;
        const bool restricted = cfg.TerrainForRmsId(command.Attribute("base_terrain", -1), base);
        int clumps = std::max(1, command.Attribute("number_of_clumps", 1));
        int budget = std::max(1, command.Attribute("number_of_tiles", 1));
        if (command.HasAttribute("set_scale_by_size")) {
            budget = budget * tiles / kReferenceTiles;
        }
        if (command.HasAttribute("set_scale_by_groups")) {
            clumps = std::max(1, clumps * tiles / kReferenceTiles);
        }
        const int per_clump = std::max(1, budget / clumps);

        // The ground the hills take: random-frontier clumps over the base
        // terrain, the way the script's terrains grow.
        std::vector<uint8_t> hill(static_cast<size_t>(tiles), 0);
        auto acceptable = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= width || y >= height) return false;
            if (hill[index(x, y)]) return false;
            if (restricted && grid.GetTile(x, y).terrain != base) return false;
            return !near_start(x, y);
        };
        for (int c = 0; c < clumps; ++c) {
            for (int attempt = 0; attempt < 24; ++attempt) {
                const int sx = static_cast<int>(rng.NextInt(static_cast<uint32_t>(width)));
                const int sy = static_cast<int>(rng.NextInt(static_cast<uint32_t>(height)));
                if (!acceptable(sx, sy)) continue;
                std::vector<std::pair<int, int>> frontier{{sx, sy}};
                int placed = 0;
                while (!frontier.empty() && placed < per_clump) {
                    const size_t pick = rng.NextInt(static_cast<uint32_t>(frontier.size()));
                    const auto tile = frontier[pick];
                    frontier[pick] = frontier.back();
                    frontier.pop_back();
                    if (!acceptable(tile.first, tile.second)) continue;
                    hill[index(tile.first, tile.second)] = 1;
                    ++placed;
                    static const int kdx[4] = {1, -1, 0, 0};
                    static const int kdy[4] = {0, 0, 1, -1};
                    for (int i = 0; i < 4; ++i) {
                        frontier.emplace_back(tile.first + kdx[i], tile.second + kdy[i]);
                    }
                }
                break;
            }
        }

        // A level for every tile in from the edge: the distance from flat
        // ground, eight ways, capped at the script's height.
        std::vector<int> distance(static_cast<size_t>(tiles), -1);
        std::deque<std::pair<int, int>> queue;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!hill[index(x, y)]) {
                    distance[index(x, y)] = 0;
                    queue.emplace_back(x, y);
                }
            }
        }
        while (!queue.empty()) {
            const auto tile = queue.front();
            queue.pop_front();
            const int d = distance[index(tile.first, tile.second)];
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = tile.first + dx;
                    const int ny = tile.second + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    if (distance[index(nx, ny)] >= 0) continue;
                    distance[index(nx, ny)] = d + 1;
                    queue.emplace_back(nx, ny);
                }
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int level = std::min(top, distance[index(x, y)]);
                Physics::Tile& tile = grid.GetTileMut(x, y);
                tile.elevation = std::max(tile.elevation, level);
            }
        }
    }
}

} // namespace Core::Elevation
