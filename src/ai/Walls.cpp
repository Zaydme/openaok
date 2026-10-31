#include "ai/Walls.h"

#include <algorithm>
#include <cstdlib>

#include "ai/Brain.h"
#include "ai/Census.h"
#include "ai/Economy.h"
#include "ai/Language.h"
#include "ai/Things.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Players.h"
#include "ecs/Vision.h"
#include "ecs/Walls.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace AI::Walls {

namespace {

namespace R = Core::Rules;
using Tile = std::pair<int, int>;

// The longest stretch of wall laid by one build-wall, in tiles, and the
// shortest straight run of wall a gate is put in.
constexpr int kStretch = 12;
constexpr int kGateRun = 4;

// What stands on a tile of a ring.
enum class Ground { Open, Closed, Planned, Walled };

int CivOf(const Turn& turn) {
    return turn.world != nullptr ? turn.world->seats[turn.player].civ : 0;
}

// The slot of a wall or gate thing for the player, or -1.
int SlotOf(Turn& turn, int thing) {
    if (turn.registry == nullptr || turn.world == nullptr || thing < 0) return -1;
    return Things::Slot(*turn.registry, *turn.world, turn.player, Lang::Things()[thing], CivOf(turn));
}

int GateThing() {
    static const int gate = Lang::FindThing("gate");
    return gate;
}

// The tiles the player's walls and gates stand on, finished or not.
struct Walled {
    std::vector<Tile> finished, planned;
    bool Has(const std::vector<Tile>& tiles, int x, int y) const {
        return std::find(tiles.begin(), tiles.end(), Tile{x, y}) != tiles.end();
    }
};

Walled WallsOf(const Turn& turn) {
    Walled out;
    const entt::registry& registry = *turn.registry;
    auto view = registry.view<const ECS::Components::Building, const ECS::Components::Identity,
                              const ECS::Components::Owner, const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id != turn.player) continue;
        const int unit = view.get<const ECS::Components::Identity>(e).unit_id;
        if (!ECS::Walls::IsWall(unit) && !ECS::Walls::IsGate(unit)) continue;
        const bool done = view.get<const ECS::Components::Building>(e).state == ECS::Components::BuildingState::Complete;
        const auto b = ECS::Buildings::FootprintBounds(view.get<const ECS::Components::Transform>(e).position, unit);
        for (int y = Physics::TileOf(b.min_y); y <= Physics::TileOf(b.max_y - Core::Fixed32(1) / Core::Fixed32(100)); ++y) {
            for (int x = Physics::TileOf(b.min_x); x <= Physics::TileOf(b.max_x - Core::Fixed32(1) / Core::Fixed32(100)); ++x) {
                (done ? out.finished : out.planned).emplace_back(x, y);
            }
        }
    }
    return out;
}

Ground GroundAt(const Turn& turn, const Walled& walls, int x, int y) {
    if (walls.Has(walls.finished, x, y)) return Ground::Walled;
    if (walls.Has(walls.planned, x, y)) return Ground::Planned;
    return turn.grid->IsWalkable(x, y) ? Ground::Open : Ground::Closed;
}

// Whether a wall of `slot` could be begun on the tile now.
bool Layable(const Turn& turn, int slot, int x, int y) {
    return ECS::Buildings::Check(*turn.registry, *turn.grid, turn.player, slot, x, y,
                                 ECS::Buildings::Ask::Placing) == ECS::Buildings::Site::Clear;
}

// The ring's sides, each a straight run of tiles from corner to corner.
std::vector<std::vector<Tile>> Sides(Turn& turn, int perimeter) {
    std::vector<std::vector<Tile>> sides;
    const std::vector<Tile> ring = Ring(turn, perimeter);
    for (const Tile& t : ring) {
        if (sides.empty()) {
            sides.push_back({t});
            continue;
        }
        std::vector<Tile>& side = sides.back();
        // A turn of the ring's direction starts the next side.
        if (side.size() >= 2) {
            const int dx = side[1].first - side[0].first, dy = side[1].second - side[0].second;
            const Tile& last = side.back();
            if (t.first - last.first != dx || t.second - last.second != dy) {
                sides.push_back({last, t});
                continue;
            }
        }
        side.push_back(t);
    }
    return sides;
}

const R::UnitRules* RulesOf(const Turn& turn, int slot) {
    return slot >= 0 ? ECS::Players::UnitOf(*turn.registry, turn.player, slot) : nullptr;
}

} // namespace

std::vector<Tile> Ring(Turn& turn, int perimeter) {
    std::vector<Tile> out;
    if ((perimeter != 1 && perimeter != 2) || turn.brain == nullptr || turn.census == nullptr || turn.grid == nullptr) {
        return out;
    }
    Brain& brain = *turn.brain;
    // About where the first Town Center stood, kept as it moves on.
    if (brain.wall_x < 0) {
        if (turn.census->town_center == entt::null) return out;
        brain.wall_x = Physics::TileOf(turn.census->home.x);
        brain.wall_y = Physics::TileOf(turn.census->home.y);
    }
    const int h = perimeter == 1 ? kNearRing : kFarRing;
    const int x0 = brain.wall_x - h, x1 = brain.wall_x + h, y0 = brain.wall_y - h, y1 = brain.wall_y + h;
    auto add = [&](int x, int y) {
        if (turn.grid->InBounds(x, y)) out.emplace_back(x, y);
    };
    for (int x = x0; x < x1; ++x) add(x, y0);
    for (int y = y0; y < y1; ++y) add(x1, y);
    for (int x = x1; x > x0; --x) add(x, y1);
    for (int y = y1; y > y0; --y) add(x0, y);
    return out;
}

int CompletedPercent(Turn& turn, int perimeter) {
    const std::vector<Tile> ring = Ring(turn, perimeter);
    if (ring.empty()) return 0;
    const Walled walls = WallsOf(turn);
    int done = 0;
    for (const auto& [x, y] : ring) {
        const Ground g = GroundAt(turn, walls, x, y);
        if (g == Ground::Walled || g == Ground::Closed) ++done;
    }
    return done * 100 / static_cast<int>(ring.size());
}

int InvisiblePercent(Turn& turn, int perimeter) {
    const std::vector<Tile> ring = Ring(turn, perimeter);
    if (ring.empty()) return 0;
    int unseen = 0;
    for (const auto& [x, y] : ring) {
        if (!ECS::Vision::Explored(*turn.registry, turn.player, x, y)) ++unseen;
    }
    return unseen * 100 / static_cast<int>(ring.size());
}

bool CanBuildWall(Turn& turn, int perimeter, int wall, bool with_escrow) {
    const int slot = SlotOf(turn, wall);
    const R::UnitRules* rules = RulesOf(turn, slot);
    if (rules == nullptr || !Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, with_escrow)) {
        return false;
    }
    for (const auto& [x, y] : Ring(turn, perimeter)) {
        if (Layable(turn, slot, x, y)) return true;
    }
    return false;
}

bool CanAffordComplete(Turn& turn, int perimeter, int wall) {
    const int slot = SlotOf(turn, wall);
    const R::UnitRules* rules = RulesOf(turn, slot);
    if (rules == nullptr) return false;
    const Walled walls = WallsOf(turn);
    int open = 0;
    for (const auto& [x, y] : Ring(turn, perimeter)) open += GroundAt(turn, walls, x, y) == Ground::Open ? 1 : 0;
    for (int i = 0; i < rules->cost_count; ++i) {
        const R::Cost& c = rules->costs[i];
        if (!c.paid || c.resource < 0 || c.resource > 3) continue;
        const auto r = static_cast<size_t>(R::StockpileOf(c.resource));
        if (turn.census->stock[r] < c.amount * open) return false;
    }
    return true;
}

bool BuildWall(Turn& turn, int perimeter, int wall) {
    const int slot = SlotOf(turn, wall);
    const R::UnitRules* rules = RulesOf(turn, slot);
    if (rules == nullptr || !Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, false)) {
        return false;
    }
    // The first open stretch of a side, in the ring's order.
    for (const std::vector<Tile>& side : Sides(turn, perimeter)) {
        size_t first = 0;
        while (first < side.size() && !Layable(turn, slot, side[first].first, side[first].second)) ++first;
        if (first == side.size()) continue;
        size_t last = first;
        while (last + 1 < side.size() && last + 1 - first < kStretch &&
               Layable(turn, slot, side[last + 1].first, side[last + 1].second)) {
            ++last;
        }
        const Tile from = side[first], to = side[last];
        const entt::entity worker =
            Economy::PickWorker(turn, {Core::Fixed32(from.first), Core::Fixed32(from.second)});
        if (worker == entt::null) return false;
        Give(*turn.registry, *turn.grid, Core::WallCommand{{worker}, slot, from.first, from.second, to.first, to.second,
                                                          turn.player});
        std::erase(turn.census->idle_villagers, worker);
        ++turn.census->builders;
        return true;
    }
    return false;
}

namespace {

// Where a gate of the player's goes in the ring: the middle of the first
// straight run of its finished wall long enough, with no gate in it yet.
bool GateSite(Turn& turn, int perimeter, int& slot, int& out_x, int& out_y) {
    slot = SlotOf(turn, GateThing());
    if (slot < 0) return false;
    const Walled walls = WallsOf(turn);
    const entt::registry& registry = *turn.registry;
    for (const std::vector<Tile>& side : Sides(turn, perimeter)) {
        size_t run = 0;
        for (size_t i = 0; i <= side.size(); ++i) {
            const bool walled = i < side.size() && walls.Has(walls.finished, side[i].first, side[i].second);
            const bool gated = i < side.size() && walls.Has(walls.planned, side[i].first, side[i].second);
            if (walled && !gated) {
                ++run;
                continue;
            }
            if (run >= static_cast<size_t>(kGateRun)) {
                const Tile mid = side[i - run / 2 - 1];
                const int unit = ECS::Walls::GateFor(registry, turn.player, slot, mid.first, mid.second);
                if (ECS::Walls::SiteOpen(registry, *turn.grid, turn.player, unit, mid.first, mid.second)) {
                    out_x = mid.first;
                    out_y = mid.second;
                    return true;
                }
            }
            run = 0;
        }
    }
    return false;
}

} // namespace

bool CanBuildGate(Turn& turn, int perimeter, bool with_escrow) {
    int slot = -1, x = 0, y = 0;
    if (!GateSite(turn, perimeter, slot, x, y)) return false;
    const R::UnitRules* rules = RulesOf(turn, slot);
    return rules != nullptr && Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, with_escrow);
}

bool BuildGate(Turn& turn, int perimeter) {
    int slot = -1, x = 0, y = 0;
    if (!GateSite(turn, perimeter, slot, x, y)) return false;
    const R::UnitRules* rules = RulesOf(turn, slot);
    if (rules == nullptr || !Things::Affords(*turn.census, *turn.mind, rules->costs, rules->cost_count, false)) {
        return false;
    }
    const entt::entity worker = Economy::PickWorker(turn, {Core::Fixed32(x), Core::Fixed32(y)});
    if (worker == entt::null) return false;
    Give(*turn.registry, *turn.grid, Core::BuildCommand{worker, slot, x, y, turn.player});
    std::erase(turn.census->idle_villagers, worker);
    ++turn.census->builders;
    return true;
}

void Reserve(Turn& turn, int perimeter) {
    if (turn.brain != nullptr && (perimeter == 1 || perimeter == 2)) turn.brain->wall_perimeter = perimeter;
}

} // namespace AI::Walls
