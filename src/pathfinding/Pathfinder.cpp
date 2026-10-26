#include "Pathfinder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace Pathfinding {

namespace {

// Costs are integers so the search is exactly reproducible: lockstep
// multiplayer requires every machine to walk the same path.
constexpr int kStraight = 10;
constexpr int kDiagonal = 14;

// Caps the work per request; a unit that cannot find a path within it does
// not move.
constexpr int kMaxExpansions = 20000;

struct Node {
    int32_t index;
    int32_t g_cost;
    int32_t h_cost;
    int32_t f_cost() const { return g_cost + h_cost; }

    // Strict deterministic ordering: ties are broken the same way on every
    // machine.
    bool operator>(const Node& other) const {
        if (f_cost() != other.f_cost()) return f_cost() > other.f_cost();
        if (h_cost != other.h_cost) return h_cost > other.h_cost;
        return index > other.index;
    }
};

// Scratch reused between searches; a generation stamp avoids clearing it.
struct Scratch {
    int width = 0;
    int height = 0;
    uint32_t generation = 0;
    std::vector<uint32_t> seen;      // generation stamp per tile
    std::vector<int32_t> g_score;
    std::vector<int32_t> came_from;
    std::vector<uint8_t> closed;

    void Prepare(int w, int h) {
        if (width != w || height != h) {
            width = w;
            height = h;
            const size_t n = static_cast<size_t>(w) * h;
            seen.assign(n, 0);
            g_score.assign(n, 0);
            came_from.assign(n, -1);
            closed.assign(n, 0);
            generation = 0;
        }
        ++generation;
    }

    bool Visited(int32_t i) const { return seen[i] == generation; }
    void Touch(int32_t i) {
        if (seen[i] != generation) {
            seen[i] = generation;
            g_score[i] = INT32_MAX;
            came_from[i] = -1;
            closed[i] = 0;
        }
    }
};

Scratch& GetScratch() {
    static Scratch scratch;
    return scratch;
}

// Which connected stretch of walkable ground each tile is in, so a route that
// cannot exist is refused at once. Worked out again only when the layer's
// walkability has changed. Four-way connected, matching the search, which
// never cuts a blocked corner. One labelling per layer.
struct Regions {
    uint32_t serial = 0;
    uint32_t revision = 0;
    std::vector<int32_t> label;
    std::vector<int32_t> size;   // tiles in each region, by label
};

// A few grids' worth per layer, so worlds run side by side (a lockstep twin,
// a host and its client) each keep their own labels. The least recently used
// slot gives way.
constexpr int kGridsCached = 4;

// Whether a tile joins its neighbours in a region: the layer's movers can be
// on it -- or, on land, an unlocked gate stands there. Regions are shared by
// every player, so a gate counts as open in them: for its owner the route
// through it exists, and for an enemy the search finds the gate shut.
bool Joins(const Physics::Grid& grid, Physics::Layer layer, int x, int y) {
    if (grid.Passable(layer, x, y)) return true;
    if (layer != Physics::kLand) return false;
    const Physics::Tile& tile = grid.GetTile(x, y);
    return tile.gate != 0 && (tile.gate & Physics::kGateLocked) == 0 &&
           grid.GroundPasses(Physics::kLand, tile.terrain);
}

const Regions& Labelled(const Physics::Grid& grid, Physics::Layer layer) {
    static Regions all[Physics::kMaxLayers][kGridsCached];
    static uint32_t used[Physics::kMaxLayers][kGridsCached] = {};
    static uint32_t clock = 0;
    layer = Physics::BaseLayer(layer);
    ++clock;
    int slot = 0;
    for (int i = 0; i < kGridsCached; ++i) {
        if (all[layer][i].serial == grid.Serial()) {
            slot = i;
            break;
        }
        if (used[layer][i] < used[layer][slot]) slot = i;
    }
    used[layer][slot] = clock;
    Regions& regions = all[layer][slot];
    if (regions.serial == grid.Serial() && regions.revision == grid.WalkRevision(layer) &&
        regions.label.size() == static_cast<size_t>(grid.GetWidth()) * grid.GetHeight()) {
        return regions;
    }
    const int w = grid.GetWidth();
    const int h = grid.GetHeight();
    regions.serial = grid.Serial();
    regions.revision = grid.WalkRevision(layer);
    regions.label.assign(static_cast<size_t>(w) * h, -1);
    regions.size.clear();
    std::vector<int32_t> open;
    int32_t next = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int32_t start = y * w + x;
            if (regions.label[start] >= 0 || !Joins(grid, layer, x, y)) continue;
            regions.label[start] = next;
            regions.size.push_back(1);
            open.assign(1, start);
            while (!open.empty()) {
                const int32_t at = open.back();
                open.pop_back();
                const int cx = at % w;
                const int cy = at / w;
                static constexpr int kDx[4] = {1, -1, 0, 0};
                static constexpr int kDy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + kDx[k];
                    const int ny = cy + kDy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const int32_t n = ny * w + nx;
                    if (regions.label[n] >= 0 || !Joins(grid, layer, nx, ny)) continue;
                    regions.label[n] = next;
                    ++regions.size[static_cast<size_t>(next)];
                    open.push_back(n);
                }
            }
            ++next;
        }
    }
    return regions;
}

const std::vector<int32_t>& RegionsOf(const Physics::Grid& grid, Physics::Layer layer) {
    return Labelled(grid, layer).label;
}

// Octile distance: the exact cost of an unobstructed diagonal-capable walk, so
// the heuristic never overestimates and A* stays optimal.
inline int32_t Heuristic(int x1, int y1, int x2, int y2) {
    const int dx = std::abs(x1 - x2);
    const int dy = std::abs(y1 - y2);
    return kStraight * (dx + dy) + (kDiagonal - 2 * kStraight) * std::min(dx, dy);
}

// Whether a straight line between two tiles is clear, for dropping waypoints
// the unit can simply walk past. Integer Bresenham, so it is deterministic.
bool LineIsClear(const Physics::Grid& grid, int x0, int y0, int x1, int y1,
                 Physics::Layer layer) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        if (!grid.Passable(layer, x0, y0)) return false;
        if (x0 == x1 && y0 == y1) return true;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
            // A diagonal step must not clip a blocked corner.
            if (e2 <= dx && !grid.Passable(layer, x0, y0 + sy)) return false;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

} // namespace

int RegionAt(const Physics::Grid& grid, int x, int y, Physics::Layer layer) {
    if (!grid.InBounds(x, y)) return -1;
    return RegionsOf(grid, layer)[static_cast<size_t>(y) * grid.GetWidth() + x];
}

int RegionSize(const Physics::Grid& grid, int region, Physics::Layer layer) {
    const auto& sizes = Labelled(grid, layer).size;
    if (region < 0 || region >= static_cast<int>(sizes.size())) return 0;
    return sizes[static_cast<size_t>(region)];
}

bool Approachable(const Physics::Grid& grid, int region, int x, int y, Physics::Layer layer) {
    if (region < 0) return false;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (RegionAt(grid, x + ox, y + oy, layer) == region) return true;
        }
    }
    return false;
}

ECS::Components::Path AStar::FindPath(const Physics::Grid& grid,
                                      int start_x, int start_y,
                                      int end_x, int end_y, Physics::Layer layer) {
    ECS::Components::Path path;
    if (!grid.Passable(layer, start_x, start_y) || !grid.Passable(layer, end_x, end_y)) {
        return path;
    }
    if (start_x == end_x && start_y == end_y) return path;

    const int w = grid.GetWidth();
    const int h = grid.GetHeight();
    // Two stretches of ground no walk joins: no route, and no search for one.
    {
        const auto& region = RegionsOf(grid, layer);
        if (region[static_cast<size_t>(start_y) * w + start_x] !=
            region[static_cast<size_t>(end_y) * w + end_x]) {
            return path;
        }
    }
    Scratch& s = GetScratch();
    s.Prepare(w, h);

    auto index_of = [w](int x, int y) { return static_cast<int32_t>(y) * w + x; };
    const int32_t start_index = index_of(start_x, start_y);
    const int32_t goal_index = index_of(end_x, end_y);

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    s.Touch(start_index);
    s.g_score[start_index] = 0;
    open.push({start_index, 0, Heuristic(start_x, start_y, end_x, end_y)});

    // Straights before diagonals, in a fixed order, so equal-cost expansions
    // happen identically everywhere.
    static const int dx[8] = { 0,  0,  1, -1,  1, -1,  1, -1};
    static const int dy[8] = {-1,  1,  0,  0, -1, -1,  1,  1};

    int expansions = 0;
    bool found = false;
    while (!open.empty()) {
        const Node current = open.top();
        open.pop();

        // A cheaper route to this tile was found after it was queued.
        if (s.closed[current.index]) continue;
        s.closed[current.index] = 1;

        if (current.index == goal_index) { found = true; break; }
        if (++expansions > kMaxExpansions) break;

        const int cx = current.index % w;
        const int cy = current.index / w;

        for (int i = 0; i < 8; ++i) {
            const int nx = cx + dx[i];
            const int ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (!grid.Passable(layer, nx, ny)) continue;

            // No cutting the corner of a blocked tile.
            if (i >= 4 && (!grid.Passable(layer, cx, ny) || !grid.Passable(layer, nx, cy))) {
                continue;
            }

            const int32_t neighbour = index_of(nx, ny);
            s.Touch(neighbour);
            if (s.closed[neighbour]) continue;

            const int32_t tentative = current.g_cost + (i < 4 ? kStraight : kDiagonal);
            if (tentative >= s.g_score[neighbour]) continue;

            s.g_score[neighbour] = tentative;
            s.came_from[neighbour] = current.index;
            open.push({neighbour, tentative, Heuristic(nx, ny, end_x, end_y)});
        }
    }

    if (!found) return path;

    // Walk the parent chain back to the start.
    std::vector<int32_t> tiles;
    for (int32_t i = goal_index; i != -1 && i != start_index; i = s.came_from[i]) {
        tiles.push_back(i);
    }
    std::reverse(tiles.begin(), tiles.end());
    if (tiles.empty()) return path;

    // String-pulling: keep a waypoint only where the unit actually has to
    // turn, skipping every waypoint still reachable in a straight line.
    int from_x = start_x;
    int from_y = start_y;
    size_t anchor = 0;
    while (anchor < tiles.size()) {
        size_t furthest = anchor;
        for (size_t j = tiles.size(); j-- > anchor;) {
            const int tx = tiles[j] % w;
            const int ty = tiles[j] / w;
            if (LineIsClear(grid, from_x, from_y, tx, ty, layer)) { furthest = j; break; }
        }
        const int tx = tiles[furthest] % w;
        const int ty = tiles[furthest] / w;
        path.waypoints.push_back({Core::Fixed32(tx), Core::Fixed32(ty)});
        from_x = tx;
        from_y = ty;
        anchor = furthest + 1;
    }

    return path;
}

ECS::Components::Path AStar::FindPathTo(const Physics::Grid& grid,
                                        ECS::Components::Vector2D from,
                                        ECS::Components::Vector2D to,
                                        Physics::Layer layer) {
    ECS::Components::Path path;

    const int from_x = Physics::TileOf(from.x);
    const int from_y = Physics::TileOf(from.y);
    const int to_x = Physics::TileOf(to.x);
    const int to_y = Physics::TileOf(to.y);

    if (!grid.Passable(layer, to_x, to_y)) return path;

    // Nothing to search when the destination is already in plain sight: walk
    // straight at it.
    if (LineIsClear(grid, from_x, from_y, to_x, to_y, layer)) {
        path.waypoints.push_back(to);
        path.goal = to;
        path.has_goal = true;
        return path;
    }

    path = FindPath(grid, from_x, from_y, to_x, to_y, layer);
    if (path.waypoints.empty()) return path;

    // The route is tiles; the arrival is not.
    path.waypoints.back() = to;
    // The goal is recorded at once, so a crew planning together in one tick
    // sees where each member is headed.
    path.goal = to;
    path.has_goal = true;
    return path;
}

} // namespace Pathfinding
