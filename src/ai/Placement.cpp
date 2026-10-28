#include "ai/Placement.h"

#include <algorithm>
#include <cstdlib>

#include "ai/Catalog.h"
#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/DropSites.h"
#include "ecs/Players.h"
#include "ecs/VillagerJobs.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace AI::Placement {

namespace {

namespace R = Core::Rules;
using Vector2D = ECS::Components::Vector2D;

constexpr int kFarmClass = 49;
constexpr int kForageClass = 7;
constexpr int kTreeClass = 15;
constexpr int kGoldClass = 32;
constexpr int kStoneClass = 8;
// A tree with this many blocked tiles about it, in the 5x5 square it is the
// middle of, is the edge of a forest rather than a lone tree.
constexpr int kForestTiles = 8;
// A drop site already this near a forest, mine or forage makes another
// there pointless.
const Core::Fixed32 kCovered(5);
// How many rings round a resource its drop site is looked for on.
constexpr int kBesideRings = 5;
// How far from home a dock is looked for, in tiles.
constexpr int kDockReach = 40;

// The footprint widened by a tile on every side is clear of anything standing.
// Whether `player`'s build order would take the site, as Buildings::Check
// finds it. Asked after the cheaper tests.
bool Takes(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
           int unit_id, int x, int y) {
    return ECS::Buildings::Check(registry, grid, player, unit_id, x, y, ECS::Buildings::Ask::Placing) ==
           ECS::Buildings::Site::Clear;
}

bool Breathing(const Physics::Grid& grid, int unit_id, int x, int y) {
    const auto span = ECS::Buildings::FootprintTileSpan(unit_id);
    for (int dy = span.low - 1; dy <= span.high + 1; ++dy) {
        for (int dx = span.low - 1; dx <= span.high + 1; ++dx) {
            if (!grid.InBounds(x + dx, y + dy) || grid.IsBlocked(x + dx, y + dy)) return false;
        }
    }
    return true;
}

Core::Fixed32 Distance(const Vector2D& a, const Vector2D& b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

int Sn(const Turn& turn, Lang::SN sn) { return turn.mind->sn[static_cast<size_t>(sn)]; }

int TownSize(const Turn& turn) { return std::max(8, Sn(turn, Lang::SN::MaximumTownSize)); }

const Vector2D& Where(const entt::registry& registry, entt::entity e) {
    return registry.get<ECS::Components::Transform>(e).position;
}

int HomeRegion(const Turn& turn) {
    return Pathfinding::RegionAt(*turn.grid, Physics::TileOf(turn.census->home.x) + 3,
                                 Physics::TileOf(turn.census->home.y) + 3);
}

// The nearest of the player's drop sites taking `type` -- the Town Center
// counted only if `town` -- to `at`.
Core::Fixed32 NearestDropSite(const Turn& turn, Core::ResourceType type, const Vector2D& at, bool town) {
    const entt::registry& registry = *turn.registry;
    const int villager = Catalog::Villager(registry, turn.player);
    const R::UnitRules* v = ECS::Players::UnitOf(registry, turn.player, villager);
    const int town_center = v != nullptr ? v->train_loc : -1;
    Core::Fixed32 best(1 << 20);
    for (const Census::Site& site : turn.census->buildings) {
        if (!ECS::DropSites::Accepts(site.unit_id, type)) continue;
        if (!town && site.unit_id == town_center) continue;
        const auto ground = ECS::Buildings::FootprintBounds(Where(registry, site.entity), site.unit_id);
        best = std::min(best, ground.DistanceFrom(at));
    }
    return best;
}

// How far `at` is from the player's Town Center -- from its walls, not its
// middle, as sn-camp-max-distance measures.
Core::Fixed32 FromTown(const Turn& turn, const Vector2D& at) {
    const entt::registry& registry = *turn.registry;
    if (turn.census->town_center == entt::null) return Distance(at, turn.census->home);
    const int id = registry.get<ECS::Components::Identity>(turn.census->town_center).unit_id;
    return ECS::Buildings::FootprintBounds(Where(registry, turn.census->town_center), id).DistanceFrom(at);
}

// The nearest resource of `cls` to home, seen by the player, within `reach`,
// that can be walked up to from home, not already `kCovered` by a drop site
// for `type` (the Town Center counting only if `town`), and -- for trees --
// at a forest's edge.
entt::entity Nearest(const Turn& turn, int cls, Core::ResourceType type, int reach, bool town) {
    const entt::registry& registry = *turn.registry;
    const Physics::Grid& grid = *turn.grid;
    const int region = HomeRegion(turn);
    const Vector2D& home = turn.census->home;
    entt::entity best = entt::null;
    Core::Fixed32 best_d{};
    auto stock = registry.view<const ECS::Components::Resource, const ECS::Components::Identity,
                               const ECS::Components::Transform>();
    for (auto e : stock) {
        const auto& r = stock.get<const ECS::Components::Resource>(e);
        if (r.type != type || r.current_amount <= 0) continue;
        const R::UnitRules* rules = R::Unit(stock.get<const ECS::Components::Identity>(e).unit_id);
        if (rules == nullptr || rules->cls != cls) continue;
        const Vector2D& at = stock.get<const ECS::Components::Transform>(e).position;
        const Core::Fixed32 d = Distance(at, home);
        if (best != entt::null && d >= best_d) continue;
        if (reach >= 0 && FromTown(turn, at) > Core::Fixed32(reach + kBesideRings)) continue;
        const int tx = Physics::TileOf(at.x);
        const int ty = Physics::TileOf(at.y);
        if (!ECS::Vision::Explored(registry, turn.player, tx, ty)) continue;
        if (!Pathfinding::Approachable(grid, region, tx, ty)) continue;
        if (cls == kTreeClass) {
            int blocked = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (grid.InBounds(tx + dx, ty + dy) && grid.IsBlocked(tx + dx, ty + dy)) ++blocked;
                }
            }
            if (blocked < kForestTiles) continue;
        }
        if (NearestDropSite(turn, type, at, town) <= kCovered) continue;
        best = e;
        best_d = d;
    }
    return best;
}

// A spot for a drop site beside `resource`: ring by ring outward from it,
// on each ring the spot nearest home, and none further than `reach` from the
// Town Center (-1: no limit) -- a forest just past the limit gets its camp at
// the limit, on the side facing home.
bool Beside(Turn& turn, int slot, entt::entity resource, int reach, int& x, int& y) {
    if (resource == entt::null) return false;
    const Physics::Grid& grid = *turn.grid;
    const Vector2D& at = Where(*turn.registry, resource);
    const int ax = Physics::TileOf(at.x);
    const int ay = Physics::TileOf(at.y);
    const Taken taken = TakenBy(*turn.registry, *turn.census);
    for (int breathe = 1; breathe >= 0; --breathe) {
        for (int r = 1; r <= kBesideRings; ++r) {
            bool found = false;
            Core::Fixed32 best{};
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int tx = ax + dx, ty = ay + dy;
                    if (!ECS::Buildings::CanPlace(grid, slot, tx, ty)) continue;
                    if (breathe && !Breathing(grid, slot, tx, ty)) continue;
                    if (taken.Overlaps(slot, tx, ty)) continue;
                    const Vector2D spot{Core::Fixed32(tx), Core::Fixed32(ty)};
                    if (reach >= 0 && FromTown(turn, spot) > Core::Fixed32(reach)) continue;
                    const Core::Fixed32 d = Distance(spot, turn.census->home);
                    if (found && d >= best) continue;
                    if (!Takes(*turn.registry, grid, turn.player, slot, tx, ty)) continue;
                    found = true;
                    best = d;
                    x = tx;
                    y = ty;
                }
            }
            if (found) return true;
        }
    }
    return false;
}

bool InTown(Turn& turn, int slot, int near, int& x, int& y) {
    const int town = TownSize(turn);
    return Find(*turn.registry, *turn.grid, turn.player, slot, turn.census->home, near, town, x, y, true) ||
           Find(*turn.registry, *turn.grid, turn.player, slot, turn.census->home, near, town + 10, x, y, true);
}

bool IsDock(const R::UnitRules& rules) {
    return rules.placement_terrain[0] >= 0 || rules.placement_terrain[1] >= 0;
}

// The nearest spot to home a dock may stand on, by the dat's rules, on a body
// of water of at least sn-minimum-water-body-size-for-dock and against the
// town's own land.
bool DockSpot(Turn& turn, int dock, int& out_x, int& out_y) {
    const entt::registry& registry = *turn.registry;
    const Physics::Grid& grid = *turn.grid;
    // The water its ships sail: the layer of anything the dock trains.
    int ship = -1;
    const auto& units = R::Get().units;
    for (int id = 0; id < static_cast<int>(units.size()) && ship < 0; ++id) {
        const R::UnitRules* rules = ECS::Players::UnitOf(registry, turn.player, id);
        if (rules != nullptr && rules->train_loc == dock && rules->type == R::UnitType::Creatable) ship = id;
    }
    const R::UnitRules* ship_rules = R::Unit(ship);
    if (ship_rules == nullptr) return false;
    const Physics::Layer sea = grid.LayerOf(ship_rules->terrain_restriction);
    const int water_needed = Sn(turn, Lang::SN::MinimumWaterBodySizeForDock);
    const int hx = Physics::TileOf(turn.census->home.x);
    const int hy = Physics::TileOf(turn.census->home.y);
    const int home_land = HomeRegion(turn);
    const auto span = ECS::Buildings::FootprintTileSpan(dock);
    for (int r = 0; r <= kDockReach; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int x = hx + dx, y = hy + dy;
                if (!ECS::Buildings::CanPlace(grid, dock, x, y)) continue;
                bool big_sea = false;
                bool our_land = home_land < 0;
                for (int ty = y + span.low - 1; ty <= y + span.high + 1; ++ty) {
                    for (int tx = x + span.low - 1; tx <= x + span.high + 1; ++tx) {
                        const int water = Pathfinding::RegionAt(grid, tx, ty, sea);
                        if (water >= 0 && Pathfinding::RegionSize(grid, water, sea) >= water_needed) big_sea = true;
                        if (Pathfinding::RegionAt(grid, tx, ty) == home_land) our_land = true;
                    }
                }
                if (!big_sea || !our_land) continue;
                if (!Takes(registry, grid, turn.player, dock, x, y)) continue;
                out_x = x;
                out_y = y;
                return true;
            }
        }
    }
    return false;
}

// The nearest building of a player the computer player stands as an enemy
// to, seen or not.
bool EnemyAnchor(Turn& turn, Vector2D& out) {
    const entt::registry& registry = *turn.registry;
    bool found = false;
    Core::Fixed32 best{};
    auto all = registry.view<const ECS::Components::Building, const ECS::Components::Owner,
                             const ECS::Components::Transform>();
    for (auto e : all) {
        const Core::PlayerId p = all.get<const ECS::Components::Owner>(e).player_id;
        if (p == turn.player || p == Core::PLAYER_GAIA || p >= Core::kMaxPlayers) continue;
        if (turn.mind->stance[p] != static_cast<uint8_t>(Lang::Stance::Enemy)) continue;
        if (!ECS::Combat::Alive(registry, e)) continue;
        const Vector2D& at = all.get<const ECS::Components::Transform>(e).position;
        const Core::Fixed32 d = Distance(at, turn.census->home);
        if (!found || d < best) {
            found = true;
            best = d;
            out = at;
        }
    }
    return found;
}

} // namespace

bool Taken::Overlaps(int unit_id, int x, int y) const {
    const auto span = ECS::Buildings::FootprintTileSpan(unit_id);
    const int x0 = x + span.low, x1 = x + span.high, y0 = y + span.low, y1 = y + span.high;
    for (const Rect& r : rects) {
        if (x0 <= r.x1 && r.x0 <= x1 && y0 <= r.y1 && r.y0 <= y1) return true;
    }
    return false;
}

Taken TakenBy(const entt::registry& registry, const Census& census) {
    Taken out;
    for (const Census::Site& site : census.buildings) {
        if (!registry.valid(site.entity)) continue;
        const Vector2D& at = Where(registry, site.entity);
        const auto span = ECS::Buildings::FootprintTileSpan(site.unit_id);
        const int x = Physics::TileOf(at.x), y = Physics::TileOf(at.y);
        out.rects.push_back({x + span.low, y + span.low, x + span.high, y + span.high});
    }
    return out;
}

bool Find(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player, int unit_id,
          const Vector2D& anchor, int near, int far, int& tile_x, int& tile_y, bool breathe,
          const Taken* taken) {
    const int ax = Physics::TileOf(anchor.x);
    const int ay = Physics::TileOf(anchor.y);
    for (int r = std::max(0, near); r <= far; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int x = ax + dx;
                const int y = ay + dy;
                if (!ECS::Buildings::CanPlace(grid, unit_id, x, y)) continue;
                if (breathe && !Breathing(grid, unit_id, x, y)) continue;
                if (taken != nullptr && taken->Overlaps(unit_id, x, y)) continue;
                if (!Takes(registry, grid, player, unit_id, x, y)) continue;
                tile_x = x;
                tile_y = y;
                return true;
            }
        }
    }
    return false;
}

bool Hug(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player, int unit_id,
         entt::entity building, int& tile_x, int& tile_y, const Taken* taken) {
    if (!registry.valid(building) || !registry.all_of<ECS::Components::Identity>(building)) return false;
    const int around = registry.get<ECS::Components::Identity>(building).unit_id;
    const Vector2D& origin = registry.get<ECS::Components::Transform>(building).position;
    const int bx = Physics::TileOf(origin.x);
    const int by = Physics::TileOf(origin.y);
    const auto outer = ECS::Buildings::FootprintTileSpan(around);
    const auto inner = ECS::Buildings::FootprintTileSpan(unit_id);
    // Origins whose footprint shares an edge with the building's, nearest
    // first: the four sides and the corners between.
    int best_x = 0, best_y = 0, best_d = 1 << 30;
    auto consider = [&](int x, int y) {
        const int cx = 2 * x + inner.low + inner.high;
        const int cy = 2 * y + inner.low + inner.high;
        const int mx = 2 * bx + outer.low + outer.high;
        const int my = 2 * by + outer.low + outer.high;
        const int d = (cx - mx) * (cx - mx) + (cy - my) * (cy - my);
        if (d >= best_d) return;
        if (!ECS::Buildings::CanPlace(grid, unit_id, x, y)) return;
        if (taken != nullptr && taken->Overlaps(unit_id, x, y)) return;
        if (!Takes(registry, grid, player, unit_id, x, y)) return;
        best_d = d;
        best_x = x;
        best_y = y;
    };
    const int size = inner.high - inner.low + 1;
    for (int along = outer.low - size; along <= outer.high + size; ++along) {
        consider(bx + outer.high + 1 - inner.low, by + along - inner.low);   // east
        consider(bx + outer.low - 1 - inner.high, by + along - inner.low);   // west
        consider(bx + along - inner.low, by + outer.high + 1 - inner.low);   // south
        consider(bx + along - inner.low, by + outer.low - 1 - inner.high);   // north
    }
    if (best_d == (1 << 30)) return false;
    tile_x = best_x;
    tile_y = best_y;
    return true;
}

bool Site(Turn& turn, int slot, bool forward, int& x, int& y) {
    if (turn.census->town_center == entt::null && !forward) {
        // A town with no centre builds about its villagers.
        if (turn.census->villagers.empty()) return false;
        turn.census->home = Where(*turn.registry, turn.census->villagers.front());
    }
    const entt::registry& registry = *turn.registry;
    const R::UnitRules* rules = ECS::Players::UnitOf(registry, turn.player, slot);
    if (rules == nullptr) return false;
    const Vector2D& home = turn.census->home;

    if (forward) {
        Vector2D enemy{};
        if (!EnemyAnchor(turn, enemy)) return InTown(turn, slot, 4, x, y);
        const Vector2D anchor{home.x + (enemy.x - home.x) * Core::Fixed32(2) / Core::Fixed32(3),
                              home.y + (enemy.y - home.y) * Core::Fixed32(2) / Core::Fixed32(3)};
        return Find(*turn.registry, *turn.grid, turn.player, slot, anchor, 0, 12, x, y, true) ||
               InTown(turn, slot, 4, x, y);
    }
    if (IsDock(*rules)) return DockSpot(turn, slot, x, y);
    if (rules->cls == kFarmClass) {
        const Taken taken = TakenBy(registry, *turn.census);
        if (Hug(registry, *turn.grid, turn.player, slot, turn.census->town_center, x, y, &taken)) return true;
        const int mill = Catalog::DropSite(registry, turn.player, Core::ResourceType::Food);
        for (const Census::Site& site : turn.census->buildings) {
            if (site.unit_id == mill && Hug(registry, *turn.grid, turn.player, slot, site.entity, x, y, &taken)) {
                return true;
            }
        }
        return Find(*turn.registry, *turn.grid, turn.player, slot, home, 3, TownSize(turn), x, y, false, &taken);
    }
    const int villager = Catalog::Villager(registry, turn.player);
    const R::UnitRules* v = ECS::Players::UnitOf(registry, turn.player, villager);
    // What stands once placed: a Town Center's button unit is placed as the
    // Town Center, and roles are the placed building's.
    const int placed = ECS::Buildings::PlacedUnit(slot);
    if (v != nullptr && v->train_loc == placed) {
        // Another Town Center: away from the first, out among the resources.
        return Find(*turn.registry, *turn.grid, turn.player, slot, home, 14, 40, x, y, true);
    }
    const bool wood = ECS::DropSites::Accepts(placed, Core::ResourceType::Wood);
    const bool gold = ECS::DropSites::Accepts(placed, Core::ResourceType::Gold);
    const bool stone = ECS::DropSites::Accepts(placed, Core::ResourceType::Stone);
    const bool food = ECS::DropSites::Accepts(placed, Core::ResourceType::Food);
    const int camp_reach = Sn(turn, Lang::SN::CampMaxDistance);
    if (wood && !food) {
        if (Beside(turn, slot, Nearest(turn, kTreeClass, Core::ResourceType::Wood, camp_reach, false), camp_reach, x, y)) {
            return true;
        }
        return false;
    }
    if (gold || stone) {
        const entt::entity g = gold ? Nearest(turn, kGoldClass, Core::ResourceType::Gold, camp_reach, false) : entt::null;
        if (Beside(turn, slot, g, camp_reach, x, y)) return true;
        const entt::entity s = stone ? Nearest(turn, kStoneClass, Core::ResourceType::Stone, camp_reach, false) : entt::null;
        return Beside(turn, slot, s, camp_reach, x, y);
    }
    if (food) {
        const int mill_reach = Sn(turn, Lang::SN::MillMaxDistance);
        if (Beside(turn, slot, Nearest(turn, kForageClass, Core::ResourceType::Food, mill_reach, false), mill_reach, x, y)) {
            return true;
        }
        return Find(*turn.registry, *turn.grid, turn.player, slot, home, 5, TownSize(turn), x, y, true);
    }
    return InTown(turn, slot, Catalog::House(registry, turn.player) == slot ? 3 : 5, x, y);
}

} // namespace AI::Placement
