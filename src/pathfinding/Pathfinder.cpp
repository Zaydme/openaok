#include "Pathfinder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

#include "core/Costs.h"
#include "core/FixedMath.h"

namespace Pathfinding {

namespace {

// Costs are integers so the search is exactly reproducible: lockstep
// multiplayer requires every machine to walk the same path.
constexpr int kStraight = 10;
constexpr int kDiagonal = 14;

// Caps the work per request; a unit that cannot find a path within it does
// not move.
constexpr int kMaxExpansions = 20000;
// A route shorter than kShortRoute tiles is searched within kShortBudget
// expansions and kShortBudgetPerTile more for each tile of it.
constexpr int kShortRoute = 16;
constexpr int kShortBudget = 256;
constexpr int kShortBudgetPerTile = 64;

// The route search's open-list keys: index and h in 20 bits each, f above
// them (Node's order in one integer); maps are far smaller than 2^20 tiles.
constexpr uint64_t kKeyField = (1u << 20) - 1;

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
// A tile's fields sit together, so a search reaching a tile touches one
// cache line.
struct Scratch {
    struct Cell {
        uint32_t seen = 0;       // generation stamp
        int32_t g_score = 0;
        int32_t came_from = -1;
        uint8_t closed = 0;
        // For the route search: 0 not yet asked, 1 open all round, 2 beside
        // closed ground.
        uint8_t beside = 0;
        // How many units stand on it, where `crowded` holds this search's
        // generation.
        uint32_t crowded = 0;
        int32_t standing = 0;
    };
    int width = 0;
    int height = 0;
    uint32_t generation = 0;
    std::vector<Cell> cells;

    void Prepare(int w, int h) {
        if (width != w || height != h) {
            width = w;
            height = h;
            cells.assign(static_cast<size_t>(w) * h, Cell{});
            generation = 0;
        }
        ++generation;
    }

    bool Visited(int32_t i) const { return cells[i].seen == generation; }
    void Touch(int32_t i) {
        Cell& c = cells[i];
        if (c.seen != generation) {
            c.seen = generation;
            c.g_score = INT32_MAX;
            c.came_from = -1;
            c.closed = 0;
            c.beside = 0;
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

// Landmarks for the route search's estimate: for each of a few tiles round
// the map's edge, how far every tile is from it over the layer's terrain
// alone, at the search's own step costs. Buildings, trees and crowds only
// make a route longer, so the difference between a tile's and the goal's
// distances from a landmark never overestimates the way between them, while
// knowing of the water and cliffs a straight line crosses.
int64_t g_searched = 0;

constexpr int kLandmarks = 8;
constexpr int kLandmarksUsed = 3;    // the best of them for a search
constexpr uint16_t kFar = 0xFFFF;          // a tile the landmark cannot reach
constexpr uint16_t kFarthest = kFar - 1;    // distances held at most this

struct Landmarks {
    uint32_t serial = 0;
    uint32_t revision = 0;          // the terrain revision last looked at
    int width = 0;
    int height = 0;
    std::vector<uint8_t> passes;    // per tile, whether its terrain is the layer's
    std::vector<uint16_t> distance; // per tile, its distance from each landmark
};

bool TerrainPasses(const Physics::Grid& grid, Physics::Layer layer, int x, int y) {
    return grid.GroundPasses(layer, grid.GetTile(x, y).terrain);
}

void Measure(const Physics::Grid& grid, Physics::Layer layer, Landmarks& lm) {
    const int w = grid.GetWidth();
    const int h = grid.GetHeight();
    const size_t n = static_cast<size_t>(w) * h;
    lm.width = w;
    lm.height = h;
    lm.passes.assign(n, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) lm.passes[static_cast<size_t>(y) * w + x] = TerrainPasses(grid, layer, x, y) ? 1 : 0;
    }
    lm.distance.assign(n * kLandmarks, kFar);
    // The corners and the middles of the edges, each moved to the nearest
    // tile of the layer's terrain.
    const int points[kLandmarks][2] = {{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1},
                                       {w / 2, 0}, {0, h / 2}, {w - 1, h / 2}, {w / 2, h - 1}};
    static std::vector<int32_t> dist;
    static std::vector<uint64_t> open;
    static const int dx[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const int dy[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    auto passes = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && lm.passes[static_cast<size_t>(y) * w + x] != 0;
    };
    for (int k = 0; k < kLandmarks; ++k) {
        int sx = -1, sy = -1;
        for (int r = 0; r < std::max(w, h) && sx < 0; ++r) {
            for (int oy = -r; oy <= r && sx < 0; ++oy) {
                for (int ox = -r; ox <= r; ++ox) {
                    if (std::abs(ox) != r && std::abs(oy) != r) continue;
                    if (passes(points[k][0] + ox, points[k][1] + oy)) {
                        sx = points[k][0] + ox;
                        sy = points[k][1] + oy;
                        break;
                    }
                }
            }
        }
        if (sx < 0) continue;
        dist.assign(n, INT32_MAX);
        open.clear();
        const int32_t start = sy * w + sx;
        dist[static_cast<size_t>(start)] = 0;
        open.push_back(static_cast<uint64_t>(start));
        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), std::greater<uint64_t>());
            const uint64_t key = open.back();
            open.pop_back();
            const auto at = static_cast<int32_t>(key & kKeyField);
            const auto d = static_cast<int32_t>(key >> 20);
            if (d != dist[static_cast<size_t>(at)]) continue;
            const int cx = at % w, cy = at / w;
            for (int i = 0; i < 8; ++i) {
                const int nx = cx + dx[i], ny = cy + dy[i];
                if (!passes(nx, ny)) continue;
                if (i >= 4 && (!passes(cx, ny) || !passes(nx, cy))) continue;
                const int32_t next = ny * w + nx;
                const int32_t nd = d + (i < 4 ? kStraight : kDiagonal);
                if (nd >= dist[static_cast<size_t>(next)]) continue;
                dist[static_cast<size_t>(next)] = nd;
                open.push_back((static_cast<uint64_t>(nd) << 20) | static_cast<uint64_t>(next));
                std::push_heap(open.begin(), open.end(), std::greater<uint64_t>());
            }
        }
        for (size_t t = 0; t < n; ++t) {
            if (dist[t] != INT32_MAX) {
                lm.distance[t * kLandmarks + k] = static_cast<uint16_t>(std::min<int32_t>(dist[t], kFarthest));
            }
        }
    }
}

// The layer's landmarks, measured again only when a tile's terrain has
// changed whether the layer may cross it -- as good as never in play. A few
// grids' worth, as the regions keep.
const Landmarks& LandmarksOf(const Physics::Grid& grid, Physics::Layer layer) {
    static Landmarks all[Physics::kMaxLayers][kGridsCached];
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
    Landmarks& lm = all[layer][slot];
    const int w = grid.GetWidth();
    if (lm.serial != grid.Serial() || lm.width != w || lm.height != grid.GetHeight()) {
        lm.serial = grid.Serial();
        lm.revision = grid.TerrainRevision();
        Measure(grid, layer, lm);
        return lm;
    }
    if (lm.revision != grid.TerrainRevision()) {
        const auto& log = grid.TerrainLog();
        bool changed = false;
        if (log.empty() || log.front().revision > lm.revision + 1) {
            for (int y = 0; y < lm.height && !changed; ++y) {
                for (int x = 0; x < w && !changed; ++x) {
                    changed = (TerrainPasses(grid, layer, x, y) ? 1 : 0) != lm.passes[static_cast<size_t>(y) * w + x];
                }
            }
        } else {
            for (const auto& change : log) {
                if (change.revision <= lm.revision || !grid.InBounds(change.x, change.y)) continue;
                const size_t t = static_cast<size_t>(change.y) * w + change.x;
                if ((TerrainPasses(grid, layer, change.x, change.y) ? 1 : 0) != lm.passes[t]) {
                    changed = true;
                    break;
                }
            }
        }
        lm.revision = grid.TerrainRevision();
        if (changed) Measure(grid, layer, lm);
    }
    return lm;
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

void Prepare(const Physics::Grid& grid) {
    for (int layer = 0; layer < grid.LayerCount(); ++layer) LandmarksOf(grid, static_cast<Physics::Layer>(layer));
}

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
    s.cells[start_index].g_score = 0;
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
        if (s.cells[current.index].closed) continue;
        s.cells[current.index].closed = 1;

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
            if (s.cells[neighbour].closed) continue;

            const int32_t tentative = current.g_cost + (i < 4 ? kStraight : kDiagonal);
            if (tentative >= s.cells[neighbour].g_score) continue;

            s.cells[neighbour].g_score = tentative;
            s.cells[neighbour].came_from = current.index;
            open.push({neighbour, tentative, Heuristic(nx, ny, end_x, end_y)});
        }
    }

    Core::Costs::Route(expansions);
    g_searched += expansions;
    if (!found) return path;

    // Walk the parent chain back to the start.
    std::vector<int32_t> tiles;
    for (int32_t i = goal_index; i != -1 && i != start_index; i = s.cells[i].came_from) {
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


namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;

// What a step onto a tile beside closed ground costs over the plain step, so
// routes keep a little off walls and trees; and what each unit standing on a
// tile adds, so a route goes round a standing crowd when the way round is
// short, and through it when it is not.
constexpr int32_t kBesideClosed = 4;
constexpr int32_t kPastStanding = 40;

// The least step there is: a body only touching a tile's edge is not on it.
const Fixed32 kHair(1, true);
// How far apart a line is tested, and the narrowest body tested along it, so
// no tile slips between two tests.
const Fixed32 kSampleStep = Fixed32(1) / Fixed32(4);
const Fixed32 kThinnest = Fixed32(1) / Fixed32(8);

// The tiles a body reaching `r` each way from `p` lies on.
struct TileSpan {
    int x0, y0, x1, y1;
    bool Holds(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

TileSpan Under(Vector2D p, Fixed32 r) {
    return {Physics::TileOf(p.x - r + kHair), Physics::TileOf(p.y - r + kHair),
            Physics::TileOf(p.x + r - kHair), Physics::TileOf(p.y + r - kHair)};
}

__int128_t Cross(Vector2D o, Vector2D a, Vector2D b) {
    return static_cast<__int128_t>((a.x - o.x).value) * (b.y - o.y).value -
           static_cast<__int128_t>((a.y - o.y).value) * (b.x - o.x).value;
}

// Whether the segment from a to b touches the closed box, exactly: the box
// lies across the segment's span and not wholly to one side of its line.
bool Touches(Vector2D a, Vector2D b, const Standing& q) {
    if (std::max(a.x, b.x) < q.x0 || std::min(a.x, b.x) > q.x1 ||
        std::max(a.y, b.y) < q.y0 || std::min(a.y, b.y) > q.y1) {
        return false;
    }
    if (a.x == b.x && a.y == b.y) return true;
    const Vector2D corners[4] = {{q.x0, q.y0}, {q.x1, q.y0}, {q.x1, q.y1}, {q.x0, q.y1}};
    int above = 0, below = 0;
    for (const Vector2D& c : corners) {
        const __int128_t side = Cross(a, b, c);
        if (side > 0) ++above;
        if (side < 0) ++below;
    }
    return above != 4 && below != 4;
}

Standing Grown(const Standing& q, Fixed32 by) {
    return {q.x0 - by, q.y0 - by, q.x1 + by, q.y1 + by};
}

bool Overlaps(const Standing& a, const Standing& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

// d * Fixed32(i) / Fixed32(n) for i = 0, 1, 2 ... in turn: the same values,
// stepped on by a quotient and a remainder rather than divided each time.
struct Along {
    int64_t n;
    bool negative;
    int64_t step_q, step_r;
    int64_t q = 0, r = 0;
    Along(Fixed32 d, int steps)
        : n(steps), negative(d.value < 0), step_q((negative ? -d.value : d.value) / n),
          step_r((negative ? -d.value : d.value) % n) {}
    Fixed32 Now() const { return Fixed32(negative ? -q : q, true); }
    void Next() {
        q += step_q;
        r += step_r;
        if (r >= n) {
            ++q;
            r -= n;
        }
    }
};

// Half of `v`, exactly as v / Fixed32(2) has it.
Fixed32 Half(Fixed32 v) { return Fixed32(v.value / 2, true); }

// One route's question: the ground, the body, and who stands about.
struct Walker {
    const Physics::Grid& grid;
    Physics::Layer layer;
    Fixed32 radius;
    const std::vector<Standing>* standing;
    // Per standing unit: whether lines may go through it -- it stands where
    // the walk starts or ends, or on a tile the search went across.
    std::vector<uint8_t> passed;
    // The boxes of those it may not, grown by the graze (Block).
    std::vector<Standing>& blocking;

    // A body brushing past a standing unit's box by half its own width is
    // passing it, not going through it.
    Fixed32 Graze() const { return Half(radius); }

    // Takes in `passed` as it stands.
    void Block() {
        blocking.clear();
        if (standing == nullptr) return;
        const Fixed32 graze = Graze();
        for (size_t i = 0; i < standing->size(); ++i) {
            if (passed[i] == 0) blocking.push_back(Grown((*standing)[i], graze));
        }
    }

    bool Clear(Vector2D a, Vector2D b) const {
        if (!AStar::BodyClear(grid, a, b, layer, radius)) return false;
        for (const Standing& q : blocking) {
            if (Touches(a, b, q)) return false;
        }
        return true;
    }
};

// Every tile the line from a to b crosses, in order, each passed to `visit`
// until it answers false; a line through a corner exactly takes in both tiles
// beside it. False if `visit` stopped it.
template <typename Visit>
bool AcrossTiles(Vector2D a, Vector2D b, Visit visit) {
    int cx = Physics::TileOf(a.x);
    int cy = Physics::TileOf(a.y);
    const int ex = Physics::TileOf(b.x);
    const int ey = Physics::TileOf(b.y);
    const Fixed32 dx = b.x - a.x;
    const Fixed32 dy = b.y - a.y;
    const int sx = dx.value > 0 ? 1 : dx.value < 0 ? -1 : 0;
    const int sy = dy.value > 0 ? 1 : dy.value < 0 ? -1 : 0;
    const Fixed32 half = Fixed32(1) / Fixed32(2);
    // How far along each axis to the next tile's edge the line runs into.
    Fixed32 to_x = sx > 0 ? Fixed32(cx) + half - a.x : a.x - (Fixed32(cx) - half);
    Fixed32 to_y = sy > 0 ? Fixed32(cy) + half - a.y : a.y - (Fixed32(cy) - half);
    const __int128_t adx = Core::FixedMath::Abs(dx).value;
    const __int128_t ady = Core::FixedMath::Abs(dy).value;
    const int steps = std::abs(ex - cx) + std::abs(ey - cy) + 2;
    for (int i = 0; i <= steps; ++i) {
        if (!visit(cx, cy)) return false;
        if (cx == ex && cy == ey) break;
        int along = 0;   // 1 for x, 2 for y, 3 for both
        if (sx == 0) {
            along = 2;
        } else if (sy == 0) {
            along = 1;
        } else {
            const __int128_t lhs = static_cast<__int128_t>(to_x.value) * ady;
            const __int128_t rhs = static_cast<__int128_t>(to_y.value) * adx;
            along = lhs < rhs ? 1 : lhs > rhs ? 2 : 3;
        }
        if (along == 3) {
            if (!visit(cx + sx, cy) || !visit(cx, cy + sy)) return false;
        }
        if (along & 1) {
            cx += sx;
            to_x = to_x + Fixed32(1);
        }
        if (along & 2) {
            cy += sy;
            to_y = to_y + Fixed32(1);
        }
    }
    return true;
}

} // namespace

bool AStar::MiddleClear(const Physics::Grid& grid, Vector2D a, Vector2D b, Physics::Layer layer) {
    const int ax = Physics::TileOf(a.x);
    const int ay = Physics::TileOf(a.y);
    return AcrossTiles(a, b, [&](int x, int y) {
        return (x == ax && y == ay) || grid.Passable(layer, x, y);
    });
}

bool AStar::BodyClear(const Physics::Grid& grid, Vector2D a, Vector2D b, Physics::Layer layer,
                      Fixed32 radius) {
    const Fixed32 reach = std::max(radius, kThinnest);
    const Fixed32 dx = b.x - a.x;
    const Fixed32 dy = b.y - a.y;
    const Fixed32 length = Core::FixedMath::Distance(dx, dy);
    const int samples = (length / kSampleStep).ToInt() + 1;
    // The closed tiles the body touches on the way, each with the first and
    // last test it touches it at and how many: one it touches without a break
    // from the start is one it is leaving, without a break to the end one it
    // is coming up against. Its middle never goes onto one but at either end.
    struct Touched {
        int32_t tile;
        int first, last, count;
    };
    static std::vector<Touched> touched;
    touched.clear();
    const int32_t w = grid.GetWidth();
    const int32_t from_tile = Physics::TileOf(a.y) * w + Physics::TileOf(a.x);
    const int32_t to_tile = Physics::TileOf(b.y) * w + Physics::TileOf(b.x);
    const bool middle_clear = AcrossTiles(a, b, [&](int x, int y) {
        const int32_t tile = y * w + x;
        return tile == from_tile || tile == to_tile || grid.Passable(layer, x, y);
    });
    if (!middle_clear) return false;
    Along along_x(dx, samples);
    Along along_y(dy, samples);
    // A test over the same tiles as the one before, which were all open,
    // finds them open again.
    TileSpan open{1, 1, 0, 0};
    for (int i = 0; i <= samples; ++i, along_x.Next(), along_y.Next()) {
        const Vector2D p{a.x + along_x.Now(), a.y + along_y.Now()};
        const TileSpan under = Under(p, reach);
        if (under.x0 == open.x0 && under.y0 == open.y0 && under.x1 == open.x1 && under.y1 == open.y1) continue;
        bool all_open = true;
        for (int y = under.y0; y <= under.y1; ++y) {
            for (int x = under.x0; x <= under.x1; ++x) {
                if (grid.Passable(layer, x, y)) continue;
                all_open = false;
                const int32_t tile = y * w + x;
                auto it = std::find_if(touched.begin(), touched.end(),
                                       [tile](const Touched& t) { return t.tile == tile; });
                if (it == touched.end()) {
                    touched.push_back({tile, i, i, 1});
                } else {
                    it->last = i;
                    ++it->count;
                }
            }
        }
        open = all_open ? under : TileSpan{1, 1, 0, 0};
    }
    for (const Touched& t : touched) {
        const bool unbroken = t.last - t.first + 1 == t.count;
        if (!unbroken || (t.first != 0 && t.last != samples)) return false;
    }
    return true;
}

int64_t AStar::Searched() { return g_searched; }

ECS::Components::Path AStar::FindRoute(const Physics::Grid& grid, Vector2D from, Vector2D to,
                                       Physics::Layer layer, Fixed32 radius,
                                       const std::vector<Standing>* standing) {
    ECS::Components::Path path;
    static std::vector<Standing> blocking;
    Walker walker{grid, layer, radius, standing, {}, blocking};
    const int w = grid.GetWidth();
    const int h = grid.GetHeight();
    const int start_x = Physics::TileOf(from.x);
    const int start_y = Physics::TileOf(from.y);
    const int end_x = Physics::TileOf(to.x);
    const int end_y = Physics::TileOf(to.y);
    if (!grid.InBounds(start_x, start_y) || !grid.InBounds(end_x, end_y)) return path;

    // Who the body already stands against, at either end, may be gone through.
    const Standing self_from{from.x - radius, from.y - radius, from.x + radius, from.y + radius};
    const Standing self_to{to.x - radius, to.y - radius, to.x + radius, to.y + radius};
    std::vector<int32_t> crowd;   // the tiles standing units are on, sorted
    if (standing != nullptr) {
        walker.passed.assign(standing->size(), 0);
        crowd.reserve(standing->size());
        for (size_t i = 0; i < standing->size(); ++i) {
            const Standing& q = (*standing)[i];
            if (Overlaps(q, self_from) || Overlaps(q, self_to)) walker.passed[i] = 1;
            const int qx = Physics::TileOf(Half(q.x0 + q.x1));
            const int qy = Physics::TileOf(Half(q.y0 + q.y1));
            if (grid.InBounds(qx, qy)) crowd.push_back(qy * w + qx);
        }
        std::sort(crowd.begin(), crowd.end());
    }
    walker.Block();
    auto finish = [&](std::vector<Vector2D> points) {
        path.waypoints = std::move(points);
        path.goal = path.waypoints.back();
        path.has_goal = true;
        return path;
    };
    // In plain sight: straight there.
    if (walker.Clear(from, to)) return finish({to});
    if (!grid.Passable(layer, end_x, end_y)) return path;
    {
        const auto& region = RegionsOf(grid, layer);
        const int32_t start_region = region[static_cast<size_t>(start_y) * w + start_x];
        if (start_region >= 0 && start_region != region[static_cast<size_t>(end_y) * w + end_x]) {
            return path;
        }
    }

    Scratch& sc = GetScratch();
    sc.Prepare(w, h);
    auto index_of = [w](int x, int y) { return static_cast<int32_t>(y) * w + x; };
    const int32_t start_index = index_of(start_x, start_y);
    const int32_t goal_index = index_of(end_x, end_y);
    // Worked out once a search for each tile it reaches.
    auto beside_closed = [&](int32_t index, int x, int y) {
        uint8_t& known = sc.cells[index].beside;
        if (known == 0) {
            known = 1;
            for (int oy = -1; oy <= 1 && known == 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if ((ox != 0 || oy != 0) && !grid.Passable(layer, x + ox, y + oy)) {
                        known = 2;
                        break;
                    }
                }
            }
        }
        return known == 2;
    };
    // The units standing on each tile, counted once for the search.
    for (const int32_t tile : crowd) {
        if (sc.cells[tile].crowded != sc.generation) {
            sc.cells[tile].crowded = sc.generation;
            sc.cells[tile].standing = 0;
        }
        ++sc.cells[tile].standing;
    }
    auto standing_on = [&](int32_t tile) { return sc.cells[tile].crowded == sc.generation ? sc.cells[tile].standing : 0; };

    // The open list: a node's f, h and index packed into one key, so that a
    // single comparison orders nodes as Node does; its g is f less h.
    static std::vector<uint64_t> open;
    open.clear();
    auto push = [](int32_t g, int32_t heuristic, int32_t index) {
        open.push_back((static_cast<uint64_t>(g + heuristic) << 40) | (static_cast<uint64_t>(heuristic) << 20) |
                       static_cast<uint64_t>(index));
        std::push_heap(open.begin(), open.end(), std::greater<uint64_t>());
    };
    // The estimate: the straight way's cost, or the landmarks' bound where
    // terrain stands in the way, whichever is more -- from the few landmarks
    // whose bound is best at the start.
    const Landmarks& landmarks = LandmarksOf(grid, layer);
    const uint16_t* goal_from = &landmarks.distance[static_cast<size_t>(goal_index) * kLandmarks];
    int chosen[kLandmarksUsed];
    int chosen_count = 0;
    {
        const uint16_t* start_from = &landmarks.distance[static_cast<size_t>(start_index) * kLandmarks];
        int32_t bound[kLandmarks];
        for (int k = 0; k < kLandmarks; ++k) {
            bound[k] = start_from[k] == kFar || goal_from[k] == kFar
                           ? -1
                           : std::abs(static_cast<int32_t>(start_from[k]) - static_cast<int32_t>(goal_from[k]));
        }
        for (; chosen_count < kLandmarksUsed; ++chosen_count) {
            int best = -1;
            for (int k = 0; k < kLandmarks; ++k) {
                if (bound[k] >= 0 && (best < 0 || bound[k] > bound[best])) best = k;
            }
            if (best < 0) break;
            chosen[chosen_count] = best;
            bound[best] = -1;
        }
    }
    auto estimate = [&](int x, int y, int32_t index) {
        int32_t best = Heuristic(x, y, end_x, end_y);
        const uint16_t* from_here = &landmarks.distance[static_cast<size_t>(index) * kLandmarks];
        for (int c = 0; c < chosen_count; ++c) {
            const int k = chosen[c];
            if (from_here[k] == kFar) continue;
            best = std::max(best, std::abs(static_cast<int32_t>(from_here[k]) - static_cast<int32_t>(goal_from[k])));
        }
        return best;
    };
    sc.Touch(start_index);
    sc.cells[start_index].g_score = 0;
    push(0, estimate(start_x, start_y, start_index), start_index);
    static const int dx[8] = { 0,  0,  1, -1,  1, -1,  1, -1};
    static const int dy[8] = {-1,  1,  0,  0, -1, -1,  1,  1};
    // A short way is searched within a budget of its length: one blocked by
    // a crowd is walked as far as the search got, and planned again on the
    // way.
    const int span = std::max(std::abs(end_x - start_x), std::abs(end_y - start_y));
    const int budget = span < kShortRoute ? kShortBudget + kShortBudgetPerTile * span : kMaxExpansions;
    int expansions = 0;
    int32_t reached = -1;
    int32_t nearest = start_index;
    int32_t nearest_h = estimate(start_x, start_y, start_index);
    while (!open.empty()) {
        std::pop_heap(open.begin(), open.end(), std::greater<uint64_t>());
        const uint64_t key = open.back();
        open.pop_back();
        const auto index = static_cast<int32_t>(key & kKeyField);
        const auto heuristic = static_cast<int32_t>((key >> 20) & kKeyField);
        const int32_t g = static_cast<int32_t>(key >> 40) - heuristic;
        if (sc.cells[index].closed) continue;
        sc.cells[index].closed = 1;
        if (index == goal_index) {
            reached = index;
            break;
        }
        if (heuristic < nearest_h) {
            nearest_h = heuristic;
            nearest = index;
        }
        if (++expansions > budget) break;
        const int cx = index % w;
        const int cy = index / w;
        for (int i = 0; i < 8; ++i) {
            const int nx = cx + dx[i];
            const int ny = cy + dy[i];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (!grid.Passable(layer, nx, ny)) continue;
            if (i >= 4 && (!grid.Passable(layer, cx, ny) || !grid.Passable(layer, nx, cy))) continue;
            const int32_t neighbour = index_of(nx, ny);
            sc.Touch(neighbour);
            if (sc.cells[neighbour].closed) continue;
            int32_t step = i < 4 ? kStraight : kDiagonal;
            if (beside_closed(neighbour, nx, ny)) step += kBesideClosed;
            step += kPastStanding * standing_on(neighbour);
            const int32_t tentative = g + step;
            if (tentative >= sc.cells[neighbour].g_score) continue;
            sc.cells[neighbour].g_score = tentative;
            sc.cells[neighbour].came_from = index;
            push(tentative, estimate(nx, ny, neighbour), neighbour);
        }
    }
    Core::Costs::Route(expansions);
    g_searched += expansions;
    const int32_t end = reached >= 0 ? reached : nearest;
    if (end == start_index) return path;

    // The tile centres from the start to the end, the exact destination last
    // when it was reached.
    std::vector<int32_t> tiles;
    for (int32_t i = end; i != -1 && i != start_index; i = sc.cells[i].came_from) tiles.push_back(i);
    std::reverse(tiles.begin(), tiles.end());
    std::vector<Vector2D> points;
    points.reserve(tiles.size() + 1);
    points.push_back(from);
    for (int32_t t : tiles) points.push_back({Fixed32(t % w), Fixed32(t / w)});
    if (reached >= 0) points.back() = to;
    // Those the search walked across may be walked through.
    if (standing != nullptr) {
        static std::vector<int32_t> crossed;
        crossed.assign(tiles.begin(), tiles.end());
        std::sort(crossed.begin(), crossed.end());
        for (size_t i = 0; i < standing->size(); ++i) {
            const Standing& q = (*standing)[i];
            const int32_t tile = index_of(Physics::TileOf(Half(q.x0 + q.x1)), Physics::TileOf(Half(q.y0 + q.y1)));
            if (std::binary_search(crossed.begin(), crossed.end(), tile)) walker.passed[i] = 1;
        }
        walker.Block();
    }

    // Pulled straight: from each point kept, on to the furthest of the next
    // in sight, one after another.
    std::vector<Vector2D> kept;
    size_t anchor = 0;
    while (anchor + 1 < points.size()) {
        size_t next = anchor + 1;
        while (next + 1 < points.size() && walker.Clear(points[anchor], points[next + 1])) ++next;
        kept.push_back(points[next]);
        anchor = next;
    }
    return finish(std::move(kept));
}

} // namespace Pathfinding
