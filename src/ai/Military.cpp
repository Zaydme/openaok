#include "ai/Military.h"

#include <algorithm>

#include "ai/Catalog.h"
#include "core/FixedMath.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Garrisons.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"

namespace AI::Military {

namespace {

using Vector2D = ECS::Components::Vector2D;

// How far past the town an intruder is still chased by defenders.
constexpr int kChaseBeyondTown = 6;
// How far from its group's lead an attack group looks for its next target
// before going on to the next building.
const Core::Fixed32 kNearbyTarget(12);
// Soldiers this far past the town are called back to the muster point.
constexpr int kStrayBeyondTown = 4;
// How near an idle warship an enemy has to come to be met.
const Core::Fixed32 kSeaGuard(14);
// How long a group waits for its soldiers to board, and for its transports
// to land them, in passes (seconds).
constexpr int kBoardingS = 90;
constexpr int kSailingS = 240;

enum Stage { kMarching = 0, kBoarding = 1, kAtSea = 2 };

Core::Fixed32 Distance(const Vector2D& a, const Vector2D& b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

const Vector2D& Where(const entt::registry& registry, entt::entity e) {
    return registry.get<ECS::Components::Transform>(e).position;
}

int Sn(const Turn& turn, Lang::SN sn) { return turn.mind->sn[static_cast<size_t>(sn)]; }

int TownSize(const Turn& turn) { return std::max(8, Sn(turn, Lang::SN::MaximumTownSize)); }

bool Hostile(const Turn& turn, entt::entity e) {
    const auto* owner = turn.registry->try_get<ECS::Components::Owner>(e);
    if (owner == nullptr) return false;
    const Core::PlayerId p = owner->player_id;
    if (p == turn.player || p == Core::PLAYER_GAIA || p >= Core::kMaxPlayers) return false;
    return turn.mind->stance[p] == static_cast<uint8_t>(Lang::Stance::Enemy);
}

bool Idle(const entt::registry& registry, entt::entity u) {
    const auto* path = registry.try_get<ECS::Components::Path>(u);
    if (path != nullptr && path->current_waypoint_index < path->waypoints.size()) return false;
    const auto* combat = registry.try_get<ECS::Components::Combat>(u);
    return combat == nullptr || combat->state == ECS::Components::CombatState::Idle;
}

// The ground and region a unit moves on.
struct Ground {
    Physics::Layer layer = Physics::kLand;
    int region = -1;
};

Ground GroundOf(const Turn& turn, entt::entity unit) {
    Ground g;
    if (unit == entt::null || !turn.registry->all_of<ECS::Components::Transform>(unit)) return g;
    g.layer = ECS::Navigation::LayerOf(*turn.registry, *turn.grid, unit);
    g.region = ECS::Navigation::RegionOf(*turn.registry, *turn.grid, unit);
    return g;
}

// Whether something on `ground` can get up to `target`: the tiles round a
// building's footprint, or beside a unit, on that ground's region.
bool Reaches(const Turn& turn, const Ground& ground, entt::entity target) {
    if (ground.region < 0) return true;
    const entt::registry& registry = *turn.registry;
    const Physics::Grid& grid = *turn.grid;
    const Vector2D& at = Where(registry, target);
    const int x = Physics::TileOf(at.x);
    const int y = Physics::TileOf(at.y);
    if (!registry.all_of<ECS::Components::Building>(target)) {
        return Pathfinding::RegionAt(grid, x, y, ground.layer) == ground.region ||
               Pathfinding::Approachable(grid, ground.region, x, y, ground.layer);
    }
    const auto span = ECS::Buildings::FootprintTileSpan(registry.get<ECS::Components::Identity>(target).unit_id);
    for (int dy = span.low - 1; dy <= span.high + 1; ++dy) {
        for (int dx = span.low - 1; dx <= span.high + 1; ++dx) {
            const bool edge = dx == span.low - 1 || dx == span.high + 1 || dy == span.low - 1 || dy == span.high + 1;
            if (edge && Pathfinding::RegionAt(grid, x + dx, y + dy, ground.layer) == ground.region) return true;
        }
    }
    return false;
}

// The nearest enemy within `reach` of `at`: buildings only if asked, only
// what the player can see unless `known` lets it remember any building, and
// -- given a ground -- only what can be got at from it.
entt::entity NearestEnemy(const Turn& turn, const Vector2D& at, Core::Fixed32 reach, bool buildings_only,
                          bool known, const Ground* ground) {
    const entt::registry& registry = *turn.registry;
    entt::entity best = entt::null;
    Core::Fixed32 best_d{};
    auto all = registry.view<const ECS::Components::Transform, const ECS::Components::Owner>();
    for (auto e : all) {
        if (!Hostile(turn, e) || !ECS::Combat::Alive(registry, e)) continue;
        const bool building = registry.all_of<ECS::Components::Building>(e);
        if (buildings_only && !building) continue;
        const Core::Fixed32 d = Distance(all.get<const ECS::Components::Transform>(e).position, at);
        if (d > reach) continue;
        if (best != entt::null && (d > best_d || (d == best_d && e >= best))) continue;
        if (!(known && building) && !ECS::Vision::Shows(registry, turn.player, e)) continue;
        if (ground != nullptr && !Reaches(turn, *ground, e)) continue;
        best = e;
        best_d = d;
    }
    return best;
}

// Where the army gathers: a few tiles from home toward the middle of the map.
Vector2D Muster(const Turn& turn) {
    const Physics::Grid& grid = *turn.grid;
    const Vector2D& home = turn.census->home;
    const Core::Fixed32 cx(grid.GetWidth() / 2);
    const Core::Fixed32 cy(grid.GetHeight() / 2);
    const Core::Fixed32 d = Distance(home, {cx, cy});
    if (d.value <= 0) return home;
    const Core::Fixed32 step(7);
    return {home.x + (cx - home.x) * step / d, home.y + (cy - home.y) * step / d};
}

bool Contains(const std::vector<entt::entity>& v, entt::entity e) {
    return std::find(v.begin(), v.end(), e) != v.end();
}

// Aboard one of the group's transports.
bool Aboard(const entt::registry& registry, const Group& group, entt::entity e) {
    const auto* inside = registry.try_get<ECS::Components::Garrisoned>(e);
    return inside != nullptr && Contains(group.ships, inside->building) && ECS::Combat::Alive(registry, inside->building);
}

void Prune(const entt::registry& registry, Group& group) {
    std::erase_if(group.ships, [&](entt::entity s) { return !ECS::Combat::Alive(registry, s); });
    std::erase_if(group.units, [&](entt::entity e) {
        return !ECS::Combat::Alive(registry, e) && !Aboard(registry, group, e);
    });
}

entt::entity Lead(const Turn& turn, const Group& group) {
    for (entt::entity u : group.units) {
        if (turn.registry->all_of<ECS::Components::Transform>(u)) return u;
    }
    return entt::null;
}

// The group's next target: something near its lead, else the nearest enemy
// building it has seen, else any -- of those it can get at, when `reach`.
entt::entity NextTarget(const Turn& turn, const Group& group, bool reach) {
    const entt::entity lead = Lead(turn, group);
    const Vector2D from = lead == entt::null ? turn.census->home : Where(*turn.registry, lead);
    const Ground ground = GroundOf(turn, lead);
    const Ground* on = reach ? &ground : nullptr;
    entt::entity t = NearestEnemy(turn, from, kNearbyTarget, false, false, on);
    if (t == entt::null) t = NearestEnemy(turn, from, Core::Fixed32(1 << 16), true, false, on);
    if (t == entt::null) t = NearestEnemy(turn, from, Core::Fixed32(1 << 16), true, true, on);
    return t;
}

void Charge(Turn& turn, Group& group, bool idle_only) {
    for (entt::entity u : group.units) {
        if (!turn.registry->all_of<ECS::Components::Transform>(u)) continue;
        if (idle_only && !Idle(*turn.registry, u)) continue;
        Give(*turn.registry, *turn.grid, Core::AttackCommand{u, group.target, turn.player});
    }
}

void Disband(Turn& turn, Group& group) {
    const Vector2D at = Muster(turn);
    for (entt::entity u : group.units) {
        if (!turn.registry->all_of<ECS::Components::Transform>(u)) continue;
        Give(*turn.registry, *turn.grid, Core::MoveCommand{u, at.x, at.y, turn.player});
    }
    group.units.clear();
    group.ships.clear();
    group.target = entt::null;
    group.stage = kMarching;
}

// A target across the water: the group boards the player's idle transports
// -- as many soldiers as they hold -- to be landed at the shore nearest it.
bool Board(Turn& turn, Group& group) {
    entt::registry& registry = *turn.registry;
    group.ships.clear();
    std::vector<int> room;
    for (entt::entity ship : turn.census->transports) {
        if (!registry.all_of<ECS::Components::Transform>(ship) || !Idle(registry, ship)) continue;
        const int free = ECS::Garrisons::Capacity(registry, ship) - ECS::Garrisons::Count(registry, ship);
        if (free <= 0) continue;
        group.ships.push_back(ship);
        room.push_back(free);
    }
    if (group.ships.empty()) return false;
    std::vector<entt::entity> boarding;
    size_t s = 0;
    for (entt::entity u : group.units) {
        while (s < group.ships.size() && room[s] == 0) ++s;
        if (s >= group.ships.size()) break;
        Give(registry, *turn.grid, Core::GarrisonCommand{u, group.ships[s], turn.player});
        --room[s];
        boarding.push_back(u);
    }
    group.units = boarding;
    group.stage = kBoarding;
    group.stage_s = 0;
    return !group.units.empty();
}

void Upkeep(Turn& turn, Group& group) {
    entt::registry& registry = *turn.registry;
    Prune(registry, group);
    if (group.units.empty()) {
        group.target = entt::null;
        group.ships.clear();
        group.stage = kMarching;
        return;
    }
    if (group.stage == kBoarding) {
        ++group.stage_s;
        bool outside = false;
        for (entt::entity u : group.units) outside = outside || !Aboard(registry, group, u);
        if (outside && group.stage_s <= kBoardingS) return;
        // Those still ashore stay home; the rest sail.
        std::erase_if(group.units, [&](entt::entity u) { return !Aboard(registry, group, u); });
        if (group.units.empty() || !registry.valid(group.target) ||
            !registry.all_of<ECS::Components::Transform>(group.target)) {
            Disband(turn, group);
            return;
        }
        const Vector2D at = Where(registry, group.target);
        for (entt::entity ship : group.ships) {
            if (ECS::Garrisons::Count(registry, ship) > 0) {
                Give(registry, *turn.grid, Core::UnloadCommand{ship, at.x, at.y, turn.player});
            }
        }
        group.stage = kAtSea;
        group.stage_s = 0;
        return;
    }
    if (group.stage == kAtSea) {
        ++group.stage_s;
        bool loaded = false;
        for (entt::entity ship : group.ships) loaded = loaded || ECS::Garrisons::Count(registry, ship) > 0;
        if (loaded && group.stage_s <= kSailingS) return;
        std::erase_if(group.units, [&](entt::entity u) { return !registry.all_of<ECS::Components::Transform>(u); });
        group.ships.clear();
        group.stage = kMarching;
        group.target = entt::null;
    }
    if (!ECS::Combat::Alive(registry, group.target)) {
        group.target = NextTarget(turn, group, true);
        if (group.target == entt::null) {
            // Nothing left it can get at: home.
            Disband(turn, group);
            return;
        }
    }
    Charge(turn, group, true);
}

bool InGroup(const Group& group, entt::entity e) { return Contains(group.units, e); }

// Land soldiers at home: the army less the attack group.
std::vector<entt::entity> Defenders(const Turn& turn) {
    std::vector<entt::entity> out;
    const Census& c = *turn.census;
    for (entt::entity e : c.military) {
        if (Contains(c.boats, e)) continue;
        if (InGroup(turn.brain->attack, e)) continue;
        out.push_back(e);
    }
    return out;
}

void Defend(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    if (c.town_center == entt::null) return;
    const int town = TownSize(turn);
    // Who to meet: whoever last struck in town, while still about; else
    // anything hostile standing in it.
    entt::entity intruder = turn.brain->intruder;
    if (intruder != entt::null &&
        (!ECS::Combat::Alive(registry, intruder) ||
         Distance(Where(registry, intruder), c.home) > Core::Fixed32(town + kChaseBeyondTown))) {
        intruder = entt::null;
    }
    if (intruder == entt::null) intruder = NearestEnemy(turn, c.home, Core::Fixed32(town), false, false, nullptr);
    turn.brain->intruder = intruder;
    if (intruder == entt::null) return;
    const Vector2D& at = Where(registry, intruder);
    const Core::Fixed32 reach(town + Sn(turn, Lang::SN::EnemySightedResponseDistance));
    std::vector<entt::entity> idle;
    for (entt::entity e : Defenders(turn)) {
        if (Idle(registry, e) && Distance(Where(registry, e), at) <= reach) idle.push_back(e);
    }
    const int pct = Sn(turn, Lang::SN::PercentEnemySightedResponse);
    const size_t send = (idle.size() * static_cast<size_t>(std::max(0, pct)) + 99) / 100;
    for (size_t i = 0; i < send && i < idle.size(); ++i) {
        Give(*turn.registry, *turn.grid, Core::AttackCommand{idle[i], intruder, turn.player});
    }
}

// Warships at home meet whatever enemy comes near them that they can sail
// up to.
void GuardTheSea(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    for (entt::entity boat : turn.census->boats) {
        if (InGroup(turn.brain->boat_attack, boat) || !Idle(registry, boat)) continue;
        const Ground ground = GroundOf(turn, boat);
        const entt::entity enemy = NearestEnemy(turn, Where(registry, boat), kSeaGuard, false, false, &ground);
        if (enemy != entt::null) Give(*turn.registry, *turn.grid, Core::AttackCommand{boat, enemy, turn.player});
    }
}

void Home(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    if (c.town_center == entt::null) return;
    const Vector2D muster = Muster(turn);
    // Every building that trains soldiers sends them to the muster point.
    std::vector<int> military;
    for (const Census::Site& site : c.buildings) {
        if (!site.complete || registry.all_of<ECS::Components::RallyPoint>(site.entity)) continue;
        if (military.empty()) military = Catalog::MilitaryBuildings(registry, turn.player);
        if (std::find(military.begin(), military.end(), site.unit_id) == military.end()) continue;
        Give(*turn.registry, *turn.grid, Core::RallyCommand{site.entity, muster.x, muster.y, entt::null, turn.player});
    }
    const Core::Fixed32 stray(TownSize(turn) + kStrayBeyondTown);
    for (entt::entity e : Defenders(turn)) {
        if (!Idle(registry, e)) continue;
        if (Distance(Where(registry, e), c.home) <= stray) continue;
        Give(*turn.registry, *turn.grid, Core::MoveCommand{e, muster.x, muster.y, turn.player});
    }
}

void Launch(Turn& turn, Group& group, const std::vector<entt::entity>& home, int pct, bool land) {
    if (group.stage != kMarching) return;   // still carrying the last one
    const size_t send = pct <= 0 ? 0 : (home.size() * static_cast<size_t>(pct) + 99) / 100;
    for (size_t i = 0; i < send && i < home.size(); ++i) group.units.push_back(home[i]);
    if (group.units.empty()) return;
    if (!ECS::Combat::Alive(*turn.registry, group.target)) group.target = NextTarget(turn, group, true);
    if (group.target == entt::null && land) {
        // Nothing to walk to: across the water, if there are transports.
        group.target = NextTarget(turn, group, false);
        if (group.target == entt::null || !Board(turn, group)) {
            group.units.clear();
            group.ships.clear();
            group.target = entt::null;
            group.stage = kMarching;
        }
        return;
    }
    if (group.target == entt::null) group.units.clear();
    else Charge(turn, group, false);
}

} // namespace

bool Attacking(const Turn& turn, entt::entity unit) {
    return InGroup(turn.brain->attack, unit) || InGroup(turn.brain->boat_attack, unit);
}

void AttackNow(Turn& turn) {
    Launch(turn, turn.brain->attack, Defenders(turn), Sn(turn, Lang::SN::PercentAttackSoldiers), true);
    std::vector<entt::entity> boats;
    for (entt::entity e : turn.census->boats) {
        if (!InGroup(turn.brain->boat_attack, e)) boats.push_back(e);
    }
    Launch(turn, turn.brain->boat_attack, boats, Sn(turn, Lang::SN::PercentAttackBoats), false);
}

void Run(Turn& turn) {
    Upkeep(turn, turn.brain->attack);
    Upkeep(turn, turn.brain->boat_attack);
    Defend(turn);
    GuardTheSea(turn);
    Home(turn);
}

} // namespace AI::Military
