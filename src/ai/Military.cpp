#include "ai/Military.h"

#include <algorithm>

#include "ai/Catalog.h"
#include "ai/Knowledge.h"
#include "ai/Scouting.h"
#include "core/FixedMath.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Garrisons.h"
#include "ecs/Monks.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Relics.h"
#include "ecs/Victory.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Monk.h"
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
// The target evaluation's figures: what each part of a score is worth, the
// measure of a target the group cannot hurt or none can defend, and the
// classes and units it tells apart.
const Core::Fixed32 kPart(25);
const Core::Fixed32 kOutright(100);
const Core::Fixed32 kWallCap(99);
const Core::Fixed32 kNever(1 << 30);
constexpr int kKingClass = 59;
constexpr int kRelicMonkClass = 43;
constexpr int kWallClass = 27;
constexpr int kGateClass = 39;
constexpr int kBuildingClass = 3;
constexpr int kFarmClass = 49;
constexpr int kBoatClasses[] = {2, 20, 21, 22, 53};
constexpr int kEconomicUnits[] = {70, 562, 584, 103, 50, 209, 68};
constexpr int kMonasteryUnit = 104;
constexpr int kWonderUnit = 276;
constexpr int kDockUnit = 45;
// How near a wall must come to the warships' water to be theirs to shoot.
constexpr int kShoreWall = 3;
// sn-gather-defense-units: how often the idle soldiers are gathered, how far
// from the Town Center they may stand, and how far past the town an enemy
// makes the town under attack, when they are not.
constexpr int64_t kGatherDefenseMs = 60000;
const Core::Fixed32 kDefendedReach(7);
constexpr int kUnderAttackBeyondTown = 4;
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
                          bool known, const Ground* ground, Core::PlayerId only = 0) {
    const entt::registry& registry = *turn.registry;
    entt::entity best = entt::null;
    Core::Fixed32 best_d{};
    auto all = registry.view<const ECS::Components::Transform, const ECS::Components::Owner>();
    for (auto e : all) {
        if (!Hostile(turn, e) || !ECS::Combat::Alive(registry, e)) continue;
        if (only != 0 && all.get<const ECS::Components::Owner>(e).player_id != only) continue;
        const bool building = registry.all_of<ECS::Components::Building>(e);
        if (buildings_only && !building) continue;
        const Core::Fixed32 d = Distance(all.get<const ECS::Components::Transform>(e).position, at);
        if (d > reach) continue;
        if (best != entt::null && (d > best_d || (d == best_d && e >= best))) continue;
        if (known && building) {
            const Vector2D& at = all.get<const ECS::Components::Transform>(e).position;
            if (!Knowledge::Explored(turn, Physics::TileOf(at.x), Physics::TileOf(at.y))) continue;
        } else if (!Knowledge::Sees(turn, e)) {
            continue;
        }
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

bool Listed(const int* list, size_t n, int v) { return std::find(list, list + n, v) != list + n; }

Core::Fixed32 HitPoints(const entt::registry& registry, entt::entity e) {
    const auto* health = registry.try_get<ECS::Components::Health>(e);
    return health == nullptr ? Core::Fixed32{} : Core::Fixed32(health->current_hp) - health->wound;
}

// How much of `target` `unit` takes away a second: a blow's damage over its
// reload, or for a Monk that could convert it, a tenth of its hit points.
Core::Fixed32 Rate(const entt::registry& registry, entt::entity unit, entt::entity target) {
    const auto* rules = ECS::Combat::FightingRules(registry, unit);
    const auto* defender = ECS::Combat::DefendingRules(registry, target);
    if (rules == nullptr || defender == nullptr) return {};
    if (rules->attack_count > 0 && rules->reload > Core::Fixed32(0)) {
        return Core::Fixed32(ECS::Combat::Damage(rules->attacks, rules->attack_count, *defender)) / rules->reload;
    }
    if (ECS::Monks::CanConvert(registry, unit, target)) return HitPoints(registry, target) / Core::Fixed32(10);
    return {};
}

// One enemy thing as the target evaluation weighs it.
struct Prospect {
    entt::entity e = entt::null;
    bool reaches = false;   // the group can get at it
    bool zone = false;      // ... and stands on the group's ground
    bool economic = false;  // a house, mill, camp, farm, Blacksmith or University
    bool wall = false;      // a wall or gate
    Core::Fixed32 kill;     // seconds the group takes to destroy it, kNever if it cannot
    Core::Fixed32 defence;  // seconds its defenders take to kill the group, kNever if none
    Core::Fixed32 distance; // from the group's middle to its edge
    Core::Fixed32 score;
    int tier = 0;
};

// A measure scored from 25, for the least of the prospects', down to 0 for
// the greatest; 25 for all when they are alike. Against one the group cannot
// measure (kNever) the rest all score 25 and it 0.
Core::Fixed32 Part(Core::Fixed32 v, Core::Fixed32 low, Core::Fixed32 high) {
    if (high - low <= Core::Fixed32(0)) return kPart;
    if (high == kNever) return v == kNever ? Core::Fixed32{} : kPart;
    return kPart - (v - low) * kPart / (high - low);
}

// Whether `e` scores 100 whatever its other parts: a Regicide game's King, the
// Wonder of a player counting down to win by it, a Monastery with
// sn-special-attack-type1, and -- of a player counting down by relics -- its
// Monks carrying relics and Monasteries; a Monastery that holds none scores
// 99.9.
bool Outright(const Turn& turn, entt::entity e, int cls, int id, Core::Fixed32& score) {
    const entt::registry& registry = *turn.registry;
    const Core::PlayerId owner = registry.get<ECS::Components::Owner>(e).player_id;
    const auto* state = registry.ctx().find<ECS::Victory::State>();
    if (state != nullptr && state->setup.regicide && cls == kKingClass) {
        score = kOutright;
        return true;
    }
    if (state != nullptr && state->outcome.relic_holder == owner && (cls == kRelicMonkClass || id == kMonasteryUnit)) {
        const bool holds = cls == kRelicMonkClass || ECS::Relics::Held(registry, e) > 0;
        score = holds ? kOutright : Core::Fixed32(999) / Core::Fixed32(10);
        return true;
    }
    if ((state != nullptr && state->outcome.wonder_holder == owner && id == kWonderUnit) ||
        (id == kMonasteryUnit && Sn(turn, Lang::SN::SpecialAttackType1) == 1)) {
        score = kOutright;
        return true;
    }
    return false;
}

// The target evaluation's prospects for `group`: every enemy thing the player
// knows of -- of player `only` if given -- that the group could fight
// (warships: boats, docks and walls by their water; soldiers: anything but
// boats), weighed and scored.
std::vector<Prospect> Weigh(const Turn& turn, const Group& group, bool sea, Core::PlayerId only) {
    const entt::registry& registry = *turn.registry;
    const Physics::Grid& grid = *turn.grid;
    const ECS::Victory::Outcome* outcome = ECS::Victory::Read(registry);
    const Ground ground = GroundOf(turn, Lead(turn, group));
    std::vector<entt::entity> units;
    Vector2D middle{};
    int placed = 0;
    for (entt::entity u : group.units) {
        if (!ECS::Combat::Alive(registry, u)) continue;
        units.push_back(u);
        if (!registry.all_of<ECS::Components::Transform>(u)) continue;
        middle.x = middle.x + Where(registry, u).x;
        middle.y = middle.y + Where(registry, u).y;
        ++placed;
    }
    if (placed == 0) return {};
    middle.x = middle.x / Core::Fixed32(placed);
    middle.y = middle.y / Core::Fixed32(placed);

    std::vector<Prospect> out;
    std::vector<entt::entity> defenders;
    auto all = registry.view<const ECS::Components::Transform, const ECS::Components::Owner>();
    for (auto e : all) {
        if (!Hostile(turn, e) || !ECS::Combat::Alive(registry, e)) continue;
        const Core::PlayerId owner = all.get<const ECS::Components::Owner>(e).player_id;
        if (outcome != nullptr && outcome->Defeated(owner)) continue;
        const bool building = registry.all_of<ECS::Components::Building>(e);
        const Vector2D& at = all.get<const ECS::Components::Transform>(e).position;
        const int x = Physics::TileOf(at.x), y = Physics::TileOf(at.y);
        if (building ? !Knowledge::Explored(turn, x, y) : !Knowledge::Sees(turn, e)) continue;
        defenders.push_back(e);
        if (only != 0 && owner != only) continue;
        const auto* rules = ECS::Combat::DefendingRules(registry, e);
        const int cls = rules != nullptr ? rules->cls : -1;
        const int id = registry.get<ECS::Components::Identity>(e).unit_id;
        const bool boat = Listed(kBoatClasses, std::size(kBoatClasses), cls);
        if (sea && !boat && id != kDockUnit) {
            if (cls != kWallClass) continue;
            bool shore = false;
            for (int dy = -kShoreWall; dy <= kShoreWall && !shore; ++dy) {
                for (int dx = -kShoreWall; dx <= kShoreWall && !shore; ++dx) {
                    shore = Pathfinding::RegionAt(grid, x + dx, y + dy, ground.layer) == ground.region;
                }
            }
            if (!shore) continue;
        }
        if (!sea && boat) continue;
        Prospect p;
        p.e = e;
        p.reaches = Reaches(turn, ground, e);
        p.zone = p.reaches && !(sea && building);
        p.economic = (cls == kBuildingClass || cls == kFarmClass) &&
                     Listed(kEconomicUnits, std::size(kEconomicUnits), id);
        p.wall = cls == kWallClass || cls == kGateClass;
        Core::Fixed32 rate{};
        for (entt::entity u : units) rate = rate + Rate(registry, u, e);
        p.kill = rate > Core::Fixed32(0) ? HitPoints(registry, e) / rate : kNever;
        p.distance = ECS::Combat::BoxOf(registry, e).DistanceFrom(middle);
        out.push_back(p);
    }
    if (out.empty()) return out;

    // How long what could shoot at each prospect's spot would take to kill
    // the group, one soldier after another.
    for (Prospect& p : out) {
        const Vector2D& at = Where(registry, p.e);
        std::vector<entt::entity> near;
        for (entt::entity d : defenders) {
            const auto* rules = ECS::Combat::FightingRules(registry, d);
            if (rules != nullptr && ECS::Combat::BoxOf(registry, d).DistanceFrom(at) <= rules->max_range) near.push_back(d);
        }
        if (near.empty()) {
            p.defence = kNever;
            continue;
        }
        for (entt::entity u : units) {
            Core::Fixed32 rate{};
            for (entt::entity d : near) rate = rate + Rate(registry, d, u);
            if (rate > Core::Fixed32(0)) p.defence = p.defence + HitPoints(registry, u) / rate;
        }
    }

    Core::Fixed32 low[3] = {kNever, kNever, kNever}, high[3] = {Core::Fixed32(-1), Core::Fixed32(-1), Core::Fixed32(-1)};
    for (const Prospect& p : out) {
        const Core::Fixed32 v[3] = {p.kill, p.defence, p.distance};
        for (int i = 0; i < 3; ++i) {
            low[i] = std::min(low[i], v[i]);
            high[i] = std::max(high[i], v[i]);
        }
    }
    for (Prospect& p : out) {
        const auto* rules = ECS::Combat::DefendingRules(registry, p.e);
        const int cls = rules != nullptr ? rules->cls : -1;
        const int id = registry.get<ECS::Components::Identity>(p.e).unit_id;
        if (p.kill == kNever || !Outright(turn, p.e, cls, id, p.score)) {
            const Core::Fixed32 kill = Part(p.kill, low[0], high[0]);
            const Core::Fixed32 defence = high[1] - low[1] <= Core::Fixed32(0) ? kPart : kill;
            p.score = kill + defence + Part(p.distance, low[2], high[2]) + (p.zone ? kPart : Core::Fixed32{});
            if (cls == kWallClass && p.score >= kOutright) p.score = kWallCap;
        }
        p.tier = p.score >= kOutright ? 0 : (p.zone ? 1 : 4) + (p.wall ? 2 : (p.economic ? 1 : 0));
    }
    std::stable_sort(out.begin(), out.end(), [](const Prospect& a, const Prospect& b) {
        return a.tier != b.tier ? a.tier < b.tier : a.score > b.score;
    });
    return out;
}

// The group's next target, by the game's target evaluation (Weigh): each
// prospect scored out of 100 -- up to 25 for how soon the group would destroy
// it, 25 more for that again (or for every prospect alike when the defences
// round them all are alike), up to 25 for how near it is, and 25 if it is on
// the group's ground -- and a wall at most 99. They are taken in tiers: those
// scoring 100; then those on the group's ground -- all but walls and gates,
// the economic buildings after the rest, then walls and gates; then the same
// of those elsewhere; the highest score first in each. The first the group
// can get at is its target, or with `reach` false the first of all. The foe an
// ally's call named is looked to first.
entt::entity NextTarget(const Turn& turn, const Group& group, bool reach, bool sea) {
    const Core::PlayerId foe = turn.brain->foe;
    for (int pass = foe != 0 ? 0 : 1; pass < 2; ++pass) {
        for (const Prospect& p : Weigh(turn, group, sea, pass == 0 ? foe : Core::PlayerId{0})) {
            if (!reach || p.reaches) return p.e;
        }
    }
    return entt::null;
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

// A group on its way across the water: boarding, then at sea until landed.
// True while that takes it this pass.
bool Ferry(Turn& turn, Group& group) {
    entt::registry& registry = *turn.registry;
    if (group.stage == kBoarding) {
        ++group.stage_s;
        bool outside = false;
        for (entt::entity u : group.units) outside = outside || !Aboard(registry, group, u);
        if (outside && group.stage_s <= kBoardingS) return true;
        // Those still ashore stay home; the rest sail.
        std::erase_if(group.units, [&](entt::entity u) { return !Aboard(registry, group, u); });
        if (group.units.empty() || !registry.valid(group.target) ||
            !registry.all_of<ECS::Components::Transform>(group.target)) {
            Disband(turn, group);
            return true;
        }
        const Vector2D at = Where(registry, group.target);
        for (entt::entity ship : group.ships) {
            if (ECS::Garrisons::Count(registry, ship) > 0) {
                Give(registry, *turn.grid, Core::UnloadCommand{ship, at.x, at.y, turn.player});
            }
        }
        group.stage = kAtSea;
        group.stage_s = 0;
        return true;
    }
    if (group.stage == kAtSea) {
        ++group.stage_s;
        bool loaded = false;
        for (entt::entity ship : group.ships) loaded = loaded || ECS::Garrisons::Count(registry, ship) > 0;
        if (loaded && group.stage_s <= kSailingS) return true;
        std::erase_if(group.units, [&](entt::entity u) { return !registry.all_of<ECS::Components::Transform>(u); });
        group.ships.clear();
        group.stage = kMarching;
        group.target = entt::null;
    }
    return false;
}

bool InGroup(const Group& group, entt::entity e) { return Contains(group.units, e); }

// The land attack groups' tasks.
enum Task { kIdle = 0, kAttack = 1, kRandomMove = 2, kExplore = 3 };

// A group gathered on its leader is within sn-attack-group-gather-spacing of
// it, a tile's give either way; ten passes of gathering and it goes as it is.
constexpr int kGatheringPasses = 10;
// A group with nothing to attack goes for a random walk after this long.
constexpr int64_t kRandomMoveMs = 5000;
// Soldiers looked at to find where most stand together, and the tries for a
// random walk's end.
constexpr size_t kClusterCandidates = 40;
constexpr int kRandomMoveTries = 32;
// The lobby's Easy: from there down, Monks stay out of the attack groups.
constexpr int kEasy = 3;

// The land attack groups, or the boat attack groups.
std::vector<Group>& GroupsOf(Turn& turn, bool sea) { return sea ? turn.brain->boat_groups : turn.brain->attack_groups; }

const Group* GroupOf(const Turn& turn, entt::entity e) {
    for (const auto* groups : {&turn.brain->attack_groups, &turn.brain->boat_groups}) {
        for (const Group& g : *groups) {
            if (InGroup(g, e)) return &g;
        }
    }
    return nullptr;
}

// On its way to fight: an attack group's task under way, or being carried.
bool Out(const Group& g) { return g.task != kIdle || g.stage != kMarching; }

// Land soldiers at home: the army less those whose group is out.
std::vector<entt::entity> Defenders(const Turn& turn) {
    std::vector<entt::entity> out;
    const Census& c = *turn.census;
    for (entt::entity e : c.military) {
        if (Contains(c.boats, e)) continue;
        const Group* g = GroupOf(turn, e);
        if (g != nullptr && Out(*g)) continue;
        out.push_back(e);
    }
    return out;
}

// Land soldiers in no attack group, as the groups may take them -- Monks only
// above Easy -- or, `sea`, warships in none.
std::vector<entt::entity> Free(const Turn& turn, bool sea) {
    std::vector<entt::entity> out;
    const Census& c = *turn.census;
    const bool easy = turn.setup != nullptr && turn.setup->difficulty >= kEasy;
    for (entt::entity e : c.military) {
        if (Contains(c.boats, e) != sea || GroupOf(turn, e) != nullptr) continue;
        if (!turn.registry->all_of<ECS::Components::Transform>(e)) continue;
        if (easy && turn.registry->all_of<ECS::Components::Monk>(e)) continue;
        out.push_back(e);
    }
    return out;
}

// Of `free` (the first kClusterCandidates of them), the one with the most
// others within sn-group-form-distance on its own ground, and those others:
// up to `most` of them, taken out of `free`.
std::vector<entt::entity> Cluster(const Turn& turn, std::vector<entt::entity>& free, size_t most) {
    const entt::registry& registry = *turn.registry;
    const Core::Fixed32 reach(std::max(0, Sn(turn, Lang::SN::GroupFormDistance)));
    const size_t n = std::min(free.size(), kClusterCandidates);
    std::vector<int> region(n);
    for (size_t i = 0; i < n; ++i) region[i] = ECS::Navigation::RegionOf(registry, *turn.grid, free[i]);
    auto near = [&](size_t a, size_t b) {
        return region[a] == region[b] && Distance(Where(registry, free[a]), Where(registry, free[b])) <= reach;
    };
    size_t best = 0;
    int best_count = -1;
    for (size_t i = 0; i < n; ++i) {
        int count = 0;
        for (size_t j = 0; j < n; ++j) count += j != i && near(i, j) ? 1 : 0;
        if (count > best_count) {
            best = i;
            best_count = count;
        }
    }
    std::vector<entt::entity> out;
    if (n == 0 || most == 0) return out;
    out.push_back(free[best]);
    for (size_t j = 0; j < n && out.size() < most; ++j) {
        if (j != best && near(best, j)) out.push_back(free[j]);
    }
    std::erase_if(free, [&](entt::entity e) { return Contains(out, e); });
    return out;
}

// Whether the player knows any enemy's thing: in sight now, or a building
// where it has explored.
bool KnowsEnemy(const Turn& turn) {
    const entt::registry& registry = *turn.registry;
    auto all = registry.view<const ECS::Components::Transform, const ECS::Components::Owner>();
    for (auto e : all) {
        if (!Hostile(turn, e) || !ECS::Combat::Alive(registry, e)) continue;
        if (registry.all_of<ECS::Components::Building>(e)) {
            const Vector2D& at = all.get<const ECS::Components::Transform>(e).position;
            if (Knowledge::Explored(turn, Physics::TileOf(at.x), Physics::TileOf(at.y))) return true;
        } else if (Knowledge::Sees(turn, e)) {
            return true;
        }
    }
    return false;
}

// The initial attack delay: over once the game is sn-initial-attack-delay
// seconds old, or once another player has struck the player's things, as
// sn-initial-attack-delay-type allows -- 0 any of those, 1 the build list's
// end (the rules have none, so at once), 2 the time, 3 being struck.
void Allow(Turn& turn) {
    Brain& brain = *turn.brain;
    if (brain.attacks_allowed) return;
    const int type = Sn(turn, Lang::SN::InitialAttackDelayType);
    const bool timed = turn.now_ms / 1000 >= Sn(turn, Lang::SN::InitialAttackDelay);
    brain.attacks_allowed = type == 0 || type == 1 || (type == 2 && timed) || (type == 3 && brain.struck_by_player);
}

// How many land attack groups the soldiers make, and each new one's size:
// the explore groups have the first sn-minimum-explore-group-size of the
// soldiers each, up to sn-number-explore-groups of them; the rest make up
// to sn-number-attack-groups groups of sn-minimum-attack-group-size, each
// wanting that many and a random few more (sn-attack-group-size-randomness),
// at most sn-maximum-attack-group-size.
// The warships make up to sn-number-boat-attack-groups groups of
// sn-minimum-boat-attack-group-size, each wanting that many and a random few
// more, at most sn-maximum-boat-attack-group-size (`sea`).
void Setup(Turn& turn, bool sea) {
    Brain& brain = *turn.brain;
    std::vector<Group>& groups = GroupsOf(turn, sea);
    std::erase_if(groups, [](const Group& g) { return g.units.empty() && (g.now || g.task != kIdle); });
    int wanted = 0, least = 1, most = 1;
    if (sea) {
        int boats = static_cast<int>(turn.census->boats.size());
        for (const auto& g : brain.boat_explore_groups) boats += static_cast<int>(g.size());
        least = std::max(1, Sn(turn, Lang::SN::MinimumBoatAttackGroupSize));
        most = Sn(turn, Lang::SN::MaximumBoatAttackGroupSize);
        wanted = std::min((boats + least - 1) / least, std::max(0, Sn(turn, Lang::SN::NumberBoatAttackGroups)));
    } else {
        int soldiers = 0;
        for (entt::entity e : turn.census->military) soldiers += Contains(turn.census->boats, e) ? 0 : 1;
        if (turn.census->scout != entt::null) ++soldiers;
        for (const auto& g : brain.explore_groups) soldiers += static_cast<int>(g.size());
        const int least_explore = std::max(1, Sn(turn, Lang::SN::MinimumExploreGroupSize));
        const int explore = std::min((soldiers + least_explore - 1) / least_explore,
                                     std::max(0, Sn(turn, Lang::SN::NumberExploreGroups)));
        const int left = soldiers - explore * least_explore;
        least = std::max(1, Sn(turn, Lang::SN::MinimumAttackGroupSize));
        most = Sn(turn, Lang::SN::MaximumAttackGroupSize);
        wanted = left > 0 ? std::min((left + least - 1) / least, std::max(0, Sn(turn, Lang::SN::NumberAttackGroups))) : 0;
    }
    int routine = 0;
    for (const Group& g : groups) routine += g.now ? 0 : 1;
    while (routine < wanted) {
        Group g;
        const int spread = std::max(0, Sn(turn, Lang::SN::AttackGroupSizeRandomness));
        g.wanted = std::min(least + static_cast<int>(brain.rng.NextInt(static_cast<uint32_t>(spread + 1))),
                            std::max(least, most));
        g.tasked_ms = turn.now_ms;
        groups.push_back(g);
        ++routine;
    }
    // Too many: the last ones go, idle ones first.
    for (int pass = 0; pass < 2 && routine > wanted; ++pass) {
        for (size_t i = groups.size(); i-- > 0 && routine > wanted;) {
            const Group& g = groups[i];
            if (g.now || (pass == 0 && Out(g))) continue;
            groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(i));
            --routine;
        }
    }
}

// Idle groups short of soldiers take free ones, where most stand together.
void Fill(Turn& turn, bool sea) {
    std::vector<entt::entity> free = Free(turn, sea);
    for (Group& g : GroupsOf(turn, sea)) {
        if (g.now || g.task != kIdle || g.stage != kMarching) continue;
        if (static_cast<int>(g.units.size()) >= g.wanted || free.empty()) continue;
        for (entt::entity e : Cluster(turn, free, static_cast<size_t>(g.wanted) - g.units.size())) g.units.push_back(e);
    }
}

// The group's idle soldiers further than the gathering spacing from its
// leader walk to it.
void Gather(Turn& turn, Group& g) {
    const entt::registry& registry = *turn.registry;
    g.gathering = std::min(g.gathering + 1, 255);
    const entt::entity lead = Lead(turn, g);
    if (lead == entt::null) return;
    const Vector2D at = Where(registry, lead);
    const Core::Fixed32 spacing(Sn(turn, Lang::SN::AttackGroupGatherSpacing));
    for (entt::entity u : g.units) {
        if (u == lead || !registry.all_of<ECS::Components::Transform>(u) || !Idle(registry, u)) continue;
        if (Distance(Where(registry, u), at) - Core::Fixed32(1) <= spacing) continue;
        Give(*turn.registry, *turn.grid, Core::MoveCommand{u, at.x, at.y, turn.player});
    }
}

bool Gathered(const Turn& turn, const Group& g) {
    if (g.gathering > kGatheringPasses) return true;
    const entt::registry& registry = *turn.registry;
    const entt::entity lead = Lead(turn, g);
    if (lead == entt::null) return false;
    const Vector2D at = Where(registry, lead);
    const Core::Fixed32 spacing(Sn(turn, Lang::SN::AttackGroupGatherSpacing));
    for (entt::entity u : g.units) {
        if (!registry.all_of<ECS::Components::Transform>(u)) continue;
        if (Distance(Where(registry, u), at) - Core::Fixed32(1) > spacing) return false;
    }
    return true;
}

// An idle group, once it has its soldiers and has gathered, attacks if the
// player knows its enemy -- boats that know none explore the water instead;
// short, or not gathered, it gathers.
void Dispatch(Turn& turn, bool sea) {
    const bool known = KnowsEnemy(turn);
    for (Group& g : GroupsOf(turn, sea)) {
        if (g.task != kIdle || g.stage != kMarching || g.units.empty()) continue;
        const bool full = static_cast<int>(g.units.size()) >= g.wanted;
        if (!Gathered(turn, g)) {
            Gather(turn, g);
            continue;
        }
        if (!full) continue;
        g.wanted = static_cast<int>(g.units.size());
        if (!known) {
            if (sea) {
                g.task = kExplore;
                g.issued = false;
            }
            continue;
        }
        g.task = kAttack;
        g.issued = false;
        g.gathering = 0;
    }
}

// A random walk's end: open ground on the group leader's own.
bool RandomPoint(Turn& turn, entt::entity lead, int& x, int& y) {
    const Physics::Grid& grid = *turn.grid;
    const int region = ECS::Navigation::RegionOf(*turn.registry, grid, lead);
    for (int t = 0; t < kRandomMoveTries; ++t) {
        x = static_cast<int>(turn.brain->rng.NextInt(static_cast<uint32_t>(grid.GetWidth())));
        y = static_cast<int>(turn.brain->rng.NextInt(static_cast<uint32_t>(grid.GetHeight())));
        if (grid.IsWalkable(x, y) && Pathfinding::RegionAt(grid, x, y) == region) return true;
    }
    return false;
}

void Reset(Group& g) {
    g.task = kIdle;
    g.issued = false;
    g.target = entt::null;
    g.gathering = 0;
}

// Groups sent to attack: a target is chosen -- across the water by
// transport if need be -- only once attacks are allowed, unless attack-now
// made the group; with none, a random walk once the group has waited a
// while. Under way, a group whose target has fallen, or turned friend, goes
// idle again to regather; its idle soldiers are sent at the target again.
void Act(Turn& turn, bool sea) {
    entt::registry& registry = *turn.registry;
    Brain& brain = *turn.brain;
    const bool known = sea && KnowsEnemy(turn);
    for (Group& g : GroupsOf(turn, sea)) {
        Prune(registry, g);
        if (g.units.empty()) {
            Reset(g);
            g.stage = kMarching;
            g.ships.clear();
            continue;
        }
        if (g.task == kRandomMove) {
            bool moving = false;
            for (entt::entity u : g.units) moving = moving || !Idle(registry, u);
            if (!moving) Reset(g);
            continue;
        }
        if (g.task == kExplore) {
            // Boats looking for the enemy: on to the unexplored water
            // nearest them when they stand idle, until the enemy is known.
            if (known) {
                Reset(g);
                continue;
            }
            bool moving = false;
            for (entt::entity u : g.units) moving = moving || !Idle(registry, u);
            int x = 0, y = 0;
            if (!moving && Scouting::UnexploredWater(turn, Lead(turn, g), x, y)) {
                for (entt::entity u : g.units) {
                    Give(registry, *turn.grid, Core::MoveCommand{u, Core::Fixed32(x), Core::Fixed32(y), turn.player});
                }
            }
            continue;
        }
        if (g.task != kAttack) continue;
        if (!g.issued) {
            if (!brain.attacks_allowed && !g.now) continue;
            g.target = NextTarget(turn, g, true, sea);
            if (g.target == entt::null && sea) {
                g.task = kExplore;
                continue;
            }
            if (g.target == entt::null) {
                const entt::entity over = NextTarget(turn, g, false, sea);
                g.target = over;
                if (over != entt::null && Board(turn, g)) {
                    g.issued = true;
                    g.tasked_ms = turn.now_ms;
                    continue;
                }
                g.target = entt::null;
                int x = 0, y = 0;
                if (turn.now_ms - g.tasked_ms > kRandomMoveMs && RandomPoint(turn, Lead(turn, g), x, y)) {
                    g.task = kRandomMove;
                    g.to_x = x;
                    g.to_y = y;
                    for (entt::entity u : g.units) {
                        Give(registry, *turn.grid, Core::MoveCommand{u, Core::Fixed32(x), Core::Fixed32(y), turn.player});
                    }
                } else {
                    g.task = kIdle;
                }
                g.tasked_ms = turn.now_ms;
                continue;
            }
            g.issued = true;
            g.tasked_ms = turn.now_ms;
            Charge(turn, g, false);
            continue;
        }
        if (Ferry(turn, g)) continue;
        if (!ECS::Combat::Alive(registry, g.target) || !Hostile(turn, g.target) || Lead(turn, g) == entt::null) {
            Reset(g);
            continue;
        }
        Charge(turn, g, true);
    }
}

// Whether a struck thing of the player's is one whose blows go unanswered:
// one exploring, or -- with sn-ignore-attack-group-under-attack -- one of an
// attack group on its way to fight.
bool Unanswered(const Turn& turn, entt::entity victim) {
    if (victim == turn.census->scout) return true;
    for (const auto& g : turn.brain->explore_groups) {
        if (Contains(g, victim)) return true;
    }
    for (const auto& g : turn.brain->boat_explore_groups) {
        if (Contains(g, victim)) return true;
    }
    const Group* g = GroupOf(turn, victim);
    return g != nullptr && Out(*g) && Sn(turn, Lang::SN::IgnoreAttackGroupUnderAttack) == 1;
}

// An ally's call for help against `foe` answered, once attacks are allowed:
// that player's things are gone for first, and every attack group short of
// soldiers goes with what it has.
void Help(Brain& b, Core::PlayerId foe) {
    if (!b.attacks_allowed) return;
    b.foe = foe;
    for (Group& g : b.attack_groups) {
        if (g.wanted > static_cast<int>(g.units.size())) g.wanted = static_cast<int>(g.units.size());
    }
}

// Struck by `foe`'s, the player calls on its allied computer players --
// allied both ways -- once every sn-coop-share-attacking-interval seconds,
// with sn-coop-share-attacking: through the coordinator, one of the computer
// players chosen at the first call, who calls everyone; any other calls
// those of a higher number who are not the coordinator.
void CallForHelp(Turn& turn, Core::PlayerId foe) {
    Brain& brain = *turn.brain;
    if (turn.brains == nullptr || Sn(turn, Lang::SN::CoopShareAttacking) != 1) return;
    const int64_t interval = static_cast<int64_t>(Sn(turn, Lang::SN::CoopShareAttackingInterval)) * 1000;
    if (brain.helped_ms >= 0 && turn.now_ms - brain.helped_ms < interval) return;
    brain.helped_ms = turn.now_ms;
    Brains& table = *turn.brains;
    if (table.coordinator < 0 && !table.brains.empty()) {
        const auto pick = brain.rng.NextInt(static_cast<uint32_t>(table.brains.size()));
        table.coordinator = table.brains[static_cast<size_t>(pick)].player;
    }
    for (Brain& b : table.brains) {
        if (b.player == turn.player) continue;
        if (ECS::Diplomacy::StanceOf(*turn.registry, b.player, turn.player) != ECS::Diplomacy::Stance::Ally ||
            ECS::Diplomacy::StanceOf(*turn.registry, turn.player, b.player) != ECS::Diplomacy::Stance::Ally) {
            continue;
        }
        const bool coordinator = turn.player == table.coordinator;
        if (coordinator || (turn.player < b.player && b.player != table.coordinator)) Help(b, foe);
    }
}

// The blows of this pass answered: of the idle soldiers at home within
// sn-enemy-sighted-response-distance of the one struck,
// sn-percent-enemy-sighted-response percent -- whole soldiers, rounded down
// -- go for whoever struck it.
void Answer(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    std::vector<entt::entity> answered;
    const Core::Fixed32 reach(Sn(turn, Lang::SN::EnemySightedResponseDistance));
    const int pct = std::max(0, Sn(turn, Lang::SN::PercentEnemySightedResponse));
    for (const Census::Strike& s : turn.census->strikes) {
        if (Contains(answered, s.attacker) || !ECS::Combat::Alive(registry, s.attacker)) continue;
        const auto* owner = registry.try_get<ECS::Components::Owner>(s.attacker);
        if (owner == nullptr || (owner->player_id != Core::PLAYER_GAIA && !Hostile(turn, s.attacker))) continue;
        if (Unanswered(turn, s.victim)) continue;
        answered.push_back(s.attacker);
        if (owner->player_id != Core::PLAYER_GAIA) CallForHelp(turn, owner->player_id);
        std::vector<entt::entity> idle;
        for (entt::entity e : Defenders(turn)) {
            if (e != s.victim && Idle(registry, e) && Distance(Where(registry, e), s.at) <= reach) idle.push_back(e);
        }
        const size_t send = idle.size() * static_cast<size_t>(pct) / 100;
        for (size_t i = 0; i < send; ++i) {
            Give(*turn.registry, *turn.grid, Core::AttackCommand{idle[i], s.attacker, turn.player});
        }
    }
}

void Defend(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    if (c.town_center == entt::null) return;
    Answer(turn);
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
        const Group* group = GroupOf(turn, boat);
        if ((group != nullptr && Out(*group)) || !Idle(registry, boat)) continue;
        const Ground ground = GroundOf(turn, boat);
        const entt::entity enemy = NearestEnemy(turn, Where(registry, boat), kSeaGuard, false, false, &ground);
        if (enemy != entt::null) Give(*turn.registry, *turn.grid, Core::AttackCommand{boat, enemy, turn.player});
    }
}

// sn-gather-defense-units: once a minute, unless an enemy is in town, the
// idle soldiers in no group further than kDefendedReach from the Town Center
// walk to it.
void GatherDefense(Turn& turn) {
    const entt::registry& registry = *turn.registry;
    Brain& brain = *turn.brain;
    const Census& c = *turn.census;
    if (c.town_center == entt::null || Sn(turn, Lang::SN::GatherDefenseUnits) != 1) return;
    if (brain.defense_gathered_ms >= 0 && turn.now_ms - brain.defense_gathered_ms < kGatherDefenseMs) return;
    brain.defense_gathered_ms = turn.now_ms;
    if (NearestEnemy(turn, c.home, Core::Fixed32(TownSize(turn) + kUnderAttackBeyondTown), false, false, nullptr) !=
        entt::null) {
        return;
    }
    const Ground home = GroundOf(turn, c.town_center);
    for (entt::entity e : Defenders(turn)) {
        if (GroupOf(turn, e) != nullptr || !Idle(registry, e)) continue;
        if (Distance(Where(registry, e), c.home) <= kDefendedReach) continue;
        if (home.region >= 0 && GroundOf(turn, e).region != home.region) continue;
        Give(*turn.registry, *turn.grid, Core::MoveCommand{e, c.home.x, c.home.y, turn.player});
    }
}

} // namespace

bool Attacking(const Turn& turn, entt::entity unit) {
    const Group* g = GroupOf(turn, unit);
    return g != nullptr && Out(*g);
}

bool InAttackGroup(const Turn& turn, entt::entity unit) { return GroupOf(turn, unit) != nullptr; }

int AttackingSoldiers(const Turn& turn) {
    int n = 0;
    for (const Group& g : turn.brain->attack_groups) n += Out(g) ? static_cast<int>(g.units.size()) : 0;
    return n;
}

int AttackingBoats(const Turn& turn) {
    int n = 0;
    for (const Group& g : turn.brain->boat_groups) n += g.task == kAttack ? static_cast<int>(g.units.size()) : 0;
    return n;
}

void AttackNow(Turn& turn) {
    // sn-percent-attack-soldiers of the free soldiers, and
    // sn-percent-attack-boats of the free warships, rounded up, in groups of
    // those standing together; they go whether or not attacks are allowed
    // yet.
    for (const bool sea : {false, true}) {
        std::vector<entt::entity> free = Free(turn, sea);
        const int pct = Sn(turn, sea ? Lang::SN::PercentAttackBoats : Lang::SN::PercentAttackSoldiers);
        size_t send = pct <= 0 ? 0 : (free.size() * static_cast<size_t>(pct) + 99) / 100;
        while (send > 0 && !free.empty()) {
            Group g;
            g.units = Cluster(turn, free, send);
            if (g.units.empty()) break;
            send -= g.units.size();
            g.wanted = static_cast<int>(g.units.size());
            g.now = true;
            g.tasked_ms = turn.now_ms;
            GroupsOf(turn, sea).push_back(std::move(g));
        }
    }
}

void Run(Turn& turn) {
    Allow(turn);
    for (const bool sea : {false, true}) {
        Setup(turn, sea);
        Fill(turn, sea);
        Dispatch(turn, sea);
        Act(turn, sea);
    }
    Defend(turn);
    GuardTheSea(turn);
    GatherDefense(turn);
}

} // namespace AI::Military
