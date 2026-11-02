#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <vector>

#include "core/GameSetup.h"
#include "ecs/Simulation.h"

namespace Verify {

namespace {

// How far a Town Center's footprint reaches from its start, in tiles.
constexpr int kTownCentre = 2;

// Whether every start can be walked to from the first, over the ground a
// villager can walk (trees, mines and cliffs in the way). The Town Center
// standing on each start is stepped over.
bool AllReachable(const ECS::Simulation& sim) {
    const auto& grid = sim.GetGrid();
    const auto& starts = sim.player_starts;
    auto open_ground = [&](int x, int y) {
        if (grid.IsWalkable(x, y)) return true;
        for (const auto& s : starts) {
            if (std::abs(x - s.first) <= kTownCentre && std::abs(y - s.second) <= kTownCentre) return true;
        }
        return false;
    };
    if (starts.size() < 2) return false;
    const int w = grid.GetWidth(), h = grid.GetHeight();
    std::vector<uint8_t> seen(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    std::queue<std::pair<int, int>> open;
    open.push(starts[0]);
    seen[static_cast<size_t>(starts[0].second) * static_cast<size_t>(w) + static_cast<size_t>(starts[0].first)] = 1;
    while (!open.empty()) {
        const auto [x, y] = open.front();
        open.pop();
        static const int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + kDx[k], ny = y + kDy[k];
            if (!grid.InBounds(nx, ny) || !open_ground(nx, ny)) continue;
            uint8_t& s = seen[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)];
            if (s) continue;
            s = 1;
            open.push({nx, ny});
        }
    }
    for (const auto& start : starts) {
        if (!seen[static_cast<size_t>(start.second) * static_cast<size_t>(w) + static_cast<size_t>(start.first)]) {
            return false;
        }
    }
    return true;
}

} // namespace

// The random map scripts' connections: on Black Forest, whose lands are
// closed in by forest, and on Rivers, whose lands the rivers part, every
// player's start can be walked to from every other's, along paths cut through
// the trees and forded over the water.
bool CheckConnections() {
    const char* verdict = nullptr;
    static const char* const kMaps[2] = {"BlackForest", "Rivers"};
    int joined = 0;
    for (const char* map : kMaps) {
        for (const uint32_t seed : {7u, 1234u}) {
            Core::GameSetup setup = Core::DefaultSetup();
            setup.map_script = map;
            setup.seed = seed;
            for (int p = 1; p <= 4; ++p) {
                setup.players[p].active = true;
                if (p > 1) setup.players[p].computer = true;
            }
            ECS::Simulation sim;
            sim.Init(setup);
            if (!AllReachable(sim)) {
                if (verdict == nullptr) {
                    verdict = std::strcmp(map, "Rivers") == 0
                        ? "a start on Rivers cannot be walked to from another"
                        : "a start on Black Forest cannot be walked to from another";
                }
            } else {
                ++joined;
            }
        }
    }
    std::printf("connections=%s joined=%d\n", verdict == nullptr ? "ok" : verdict, joined);
    return verdict == nullptr;
}

} // namespace Verify
