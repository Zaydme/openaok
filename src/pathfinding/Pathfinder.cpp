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
struct Scratch {
    int width = 0;
    int height = 0;
    uint32_t generation = 0;
    std::vector<uint32_t> seen;      // generation stamp per tile
    std::vector<int32_t> g_score;
    std::vector<int32_t> came_from;
    std::vector<uint8_t> closed;
    // Per tile, for the route search: 0 not yet asked, 1 open all round, 2
    // beside closed ground.
    std::vector<uint8_t> beside;

    void Prepare(int w, int h) {
        if (width != w || height != h) {
            width = w;
            height = h;
            const size_t n = static_cast<size_t>(w) * h;
            seen.assign(n, 0);
            g_score.assign(n, 0);
            came_from.assign(n, -1);
            closed.assign(n, 0);
            beside.assign(n, 0);
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
            beside[i] = 0;
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

    Core::Costs::Route(expansions);
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

// One route's question: the ground, the body, and who stands about.
struct Walker {
    const Physics::Grid& grid;
    Physics::Layer layer;
    Fixed32 radius;
    const std::vector<Standing>* standing;
    // Per standing unit: whether lines may go through it -- it stands where
    // the walk starts or ends, or on a tile the search went across.
    std::vector<uint8_t> passed;

    // A body brushing past a standing unit's box by half its own width is
    // passing it, not going through it.
    Fixed32 Graze() const { return radius / Fixed32(2); }

    bool Clear(Vector2D a, Vector2D b) const {
        if (!AStar::BodyClear(grid, a, b, layer, radius)) return false;
        if (standing == nullptr) return true;
        for (size_t i = 0; i < standing->size(); ++i) {
            if (passed[i] != 0) continue;
            if (Touches(a, b, Grown((*standing)[i], Graze()))) return false;
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
    for (int i = 0; i <= samples; ++i) {
        const Vector2D p{a.x + dx * Fixed32(i) / Fixed32(samples),
                         a.y + dy * Fixed32(i) / Fixed32(samples)};
        const TileSpan under = Under(p, reach);
        for (int y = under.y0; y <= under.y1; ++y) {
            for (int x = under.x0; x <= under.x1; ++x) {
                if (grid.Passable(layer, x, y)) continue;
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
    }
    for (const Touched& t : touched) {
        const bool unbroken = t.last - t.first + 1 == t.count;
        if (!unbroken || (t.first != 0 && t.last != samples)) return false;
    }
    return true;
}

ECS::Components::Path AStar::FindRoute(const Physics::Grid& grid, Vector2D from, Vector2D to,
                                       Physics::Layer layer, Fixed32 radius,
                                       const std::vector<Standing>* standing) {
    ECS::Components::Path path;
    Walker walker{grid, layer, radius, standing, {}};
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
            const int qx = Physics::TileOf((q.x0 + q.x1) / Fixed32(2));
            const int qy = Physics::TileOf((q.y0 + q.y1) / Fixed32(2));
            if (grid.InBounds(qx, qy)) crowd.push_back(qy * w + qx);
        }
        std::sort(crowd.begin(), crowd.end());
    }
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
        uint8_t& known = sc.beside[index];
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
    auto standing_on = [&](int32_t tile) {
        const auto range = std::equal_range(crowd.begin(), crowd.end(), tile);
        return static_cast<int32_t>(range.second - range.first);
    };

    // The open list: a node's f, h and index packed into one key, so that a
    // single comparison orders nodes as Node does; its g is f less h.
    static std::vector<uint64_t> open;
    open.clear();
    auto push = [](int32_t g, int32_t heuristic, int32_t index) {
        open.push_back((static_cast<uint64_t>(g + heuristic) << 40) | (static_cast<uint64_t>(heuristic) << 20) |
                       static_cast<uint64_t>(index));
        std::push_heap(open.begin(), open.end(), std::greater<uint64_t>());
    };
    sc.Touch(start_index);
    sc.g_score[start_index] = 0;
    push(0, Heuristic(start_x, start_y, end_x, end_y), start_index);
    static const int dx[8] = { 0,  0,  1, -1,  1, -1,  1, -1};
    static const int dy[8] = {-1,  1,  0,  0, -1, -1,  1,  1};
    int expansions = 0;
    int32_t reached = -1;
    int32_t nearest = start_index;
    int32_t nearest_h = Heuristic(start_x, start_y, end_x, end_y);
    while (!open.empty()) {
        std::pop_heap(open.begin(), open.end(), std::greater<uint64_t>());
        const uint64_t key = open.back();
        open.pop_back();
        const auto index = static_cast<int32_t>(key & kKeyField);
        const auto heuristic = static_cast<int32_t>((key >> 20) & kKeyField);
        const int32_t g = static_cast<int32_t>(key >> 40) - heuristic;
        if (sc.closed[index]) continue;
        sc.closed[index] = 1;
        if (index == goal_index) {
            reached = index;
            break;
        }
        if (heuristic < nearest_h) {
            nearest_h = heuristic;
            nearest = index;
        }
        if (++expansions > kMaxExpansions) break;
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
            if (sc.closed[neighbour]) continue;
            int32_t step = i < 4 ? kStraight : kDiagonal;
            if (beside_closed(neighbour, nx, ny)) step += kBesideClosed;
            step += kPastStanding * standing_on(neighbour);
            const int32_t tentative = g + step;
            if (tentative >= sc.g_score[neighbour]) continue;
            sc.g_score[neighbour] = tentative;
            sc.came_from[neighbour] = index;
            push(tentative, Heuristic(nx, ny, end_x, end_y), neighbour);
        }
    }
    Core::Costs::Route(expansions);
    const int32_t end = reached >= 0 ? reached : nearest;
    if (end == start_index) return path;

    // The tile centres from the start to the end, the exact destination last
    // when it was reached.
    std::vector<int32_t> tiles;
    for (int32_t i = end; i != -1 && i != start_index; i = sc.came_from[i]) tiles.push_back(i);
    std::reverse(tiles.begin(), tiles.end());
    std::vector<Vector2D> points;
    points.reserve(tiles.size() + 1);
    points.push_back(from);
    for (int32_t t : tiles) points.push_back({Fixed32(t % w), Fixed32(t / w)});
    if (reached >= 0) points.back() = to;
    // Those the search walked across may be walked through.
    if (standing != nullptr) {
        for (size_t i = 0; i < standing->size(); ++i) {
            const Standing& q = (*standing)[i];
            const int32_t tile = index_of(Physics::TileOf((q.x0 + q.x1) / Fixed32(2)),
                                          Physics::TileOf((q.y0 + q.y1) / Fixed32(2)));
            if (std::find(tiles.begin(), tiles.end(), tile) != tiles.end()) walker.passed[i] = 1;
        }
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
