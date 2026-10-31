#include "ai/Military.h"

#include <algorithm>

#include "ai/Catalog.h"
#include "ai/Knowledge.h"
#include "core/FixedMath.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Combat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Garrisons.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
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
// How far from its group's lead an attack group looks for its next target
// before going on to the next building.
const Core::Fixed32 kNearbyTarget(12);
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

// The group's next target: something near its lead, else the nearest enemy
// building it has seen, else any -- of those it can get at, when `reach`.
entt::entity NextTarget(const Turn& turn, const Group& group, bool reach) {
    const entt::entity lead = Lead(turn, group);
    const Vector2D from = lead == entt::null ? turn.census->home : Where(*turn.registry, lead);
    const Ground ground = GroundOf(turn, lead);
    const Ground* on = reach ? &ground : nullptr;
    // The foe an ally's call named first, then any.
    const Core::PlayerId foe = turn.brain->foe;
    entt::entity t = entt::null;
    for (int pass = foe != 0 ? 0 : 1; pass < 2 && t == entt::null; ++pass) {
        const Core::PlayerId only = pass == 0 ? foe : Core::PlayerId{0};
        t = NearestEnemy(turn, from, kNearbyTarget, false, false, on, only);
        if (t == entt::null) t = NearestEnemy(turn, from, Core::Fixed32(1 << 16), true, false, on, only);
        if (t == entt::null) t = NearestEnemy(turn, from, Core::Fixed32(1 << 16), true, true, on, only);
    }
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

void Upkeep(Turn& turn, Group& group) {
    entt::registry& registry = *turn.registry;
    Prune(registry, group);
    if (group.units.empty()) {
        group.target = entt::null;
        group.ships.clear();
        group.stage = kMarching;
        return;
    }
    if (Ferry(turn, group)) return;
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

// The land attack groups' tasks.
enum Task { kIdle = 0, kAttack = 1, kRandomMove = 2 };

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

const Group* GroupOf(const Turn& turn, entt::entity e) {
    for (const Group& g : turn.brain->attack_groups) {
        if (InGroup(g, e)) return &g;
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

// Land soldiers in no attack group, as the groups may take them: Monks only
// above Easy.
std::vector<entt::entity> Free(const Turn& turn) {
    std::vector<entt::entity> out;
    const Census& c = *turn.census;
    const bool easy = turn.setup != nullptr && turn.setup->difficulty >= kEasy;
    for (entt::entity e : c.military) {
        if (Contains(c.boats, e) || GroupOf(turn, e) != nullptr) continue;
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
void Setup(Turn& turn) {
    Brain& brain = *turn.brain;
    std::erase_if(brain.attack_groups, [](const Group& g) { return g.units.empty() && (g.now || g.task != kIdle); });
    int soldiers = 0;
    for (entt::entity e : turn.census->military) soldiers += Contains(turn.census->boats, e) ? 0 : 1;
    if (turn.census->scout != entt::null) ++soldiers;
    for (const auto& g : brain.explore_groups) soldiers += static_cast<int>(g.size());
    const int least_explore = std::max(1, Sn(turn, Lang::SN::MinimumExploreGroupSize));
    const int explore = std::min((soldiers + least_explore - 1) / least_explore,
                                 std::max(0, Sn(turn, Lang::SN::NumberExploreGroups)));
    const int left = soldiers - explore * least_explore;
    const int least = std::max(1, Sn(turn, Lang::SN::MinimumAttackGroupSize));
    const int wanted = left > 0 ? std::min((left + least - 1) / least, std::max(0, Sn(turn, Lang::SN::NumberAttackGroups))) : 0;
    int routine = 0;
    for (const Group& g : brain.attack_groups) routine += g.now ? 0 : 1;
    while (routine < wanted) {
        Group g;
        const int spread = std::max(0, Sn(turn, Lang::SN::AttackGroupSizeRandomness));
        g.wanted = std::min(least + static_cast<int>(brain.rng.NextInt(static_cast<uint32_t>(spread + 1))),
                            std::max(least, Sn(turn, Lang::SN::MaximumAttackGroupSize)));
        g.tasked_ms = turn.now_ms;
        brain.attack_groups.push_back(g);
        ++routine;
    }
    // Too many: the last ones go, idle ones first.
    for (int pass = 0; pass < 2 && routine > wanted; ++pass) {
        for (size_t i = brain.attack_groups.size(); i-- > 0 && routine > wanted;) {
            const Group& g = brain.attack_groups[i];
            if (g.now || (pass == 0 && Out(g))) continue;
            brain.attack_groups.erase(brain.attack_groups.begin() + static_cast<std::ptrdiff_t>(i));
            --routine;
        }
    }
}

// Idle groups short of soldiers take free ones, where most stand together.
void Fill(Turn& turn) {
    std::vector<entt::entity> free = Free(turn);
    for (Group& g : turn.brain->attack_groups) {
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
// player knows its enemy; short, or not gathered, it gathers.
void Dispatch(Turn& turn) {
    const bool known = KnowsEnemy(turn);
    for (Group& g : turn.brain->attack_groups) {
        if (g.task != kIdle || g.stage != kMarching || g.units.empty()) continue;
        const bool full = static_cast<int>(g.units.size()) >= g.wanted;
        if (!Gathered(turn, g)) {
            Gather(turn, g);
            continue;
        }
        if (!full) continue;
        g.wanted = static_cast<int>(g.units.size());
        if (!known) continue;
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
void Act(Turn& turn) {
    entt::registry& registry = *turn.registry;
    Brain& brain = *turn.brain;
    for (Group& g : brain.attack_groups) {
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
        if (g.task != kAttack) continue;
        if (!g.issued) {
            if (!brain.attacks_allowed && !g.now) continue;
            g.target = NextTarget(turn, g, true);
            if (g.target == entt::null) {
                const entt::entity over = NextTarget(turn, g, false);
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
        if (InGroup(turn.brain->boat_attack, boat) || !Idle(registry, boat)) continue;
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
    const Group* g = GroupOf(turn, unit);
    return (g != nullptr && Out(*g)) || InGroup(turn.brain->boat_attack, unit);
}

bool InAttackGroup(const Turn& turn, entt::entity unit) { return GroupOf(turn, unit) != nullptr; }

int AttackingSoldiers(const Turn& turn) {
    int n = 0;
    for (const Group& g : turn.brain->attack_groups) n += Out(g) ? static_cast<int>(g.units.size()) : 0;
    return n;
}

void AttackNow(Turn& turn) {
    // sn-percent-attack-soldiers of the free soldiers, rounded up, in groups
    // of those standing together; they go whether or not attacks are
    // allowed yet.
    std::vector<entt::entity> free = Free(turn);
    const int pct = Sn(turn, Lang::SN::PercentAttackSoldiers);
    size_t send = pct <= 0 ? 0 : (free.size() * static_cast<size_t>(pct) + 99) / 100;
    while (send > 0 && !free.empty()) {
        Group g;
        g.units = Cluster(turn, free, send);
        if (g.units.empty()) break;
        send -= g.units.size();
        g.wanted = static_cast<int>(g.units.size());
        g.now = true;
        g.tasked_ms = turn.now_ms;
        turn.brain->attack_groups.push_back(std::move(g));
    }
    std::vector<entt::entity> boats;
    for (entt::entity e : turn.census->boats) {
        if (!InGroup(turn.brain->boat_attack, e)) boats.push_back(e);
    }
    Launch(turn, turn.brain->boat_attack, boats, Sn(turn, Lang::SN::PercentAttackBoats), false);
}

void Run(Turn& turn) {
    Allow(turn);
    Setup(turn);
    Fill(turn);
    Dispatch(turn);
    Act(turn);
    Upkeep(turn, turn.brain->boat_attack);
    Defend(turn);
    GuardTheSea(turn);
    GatherDefense(turn);
}

} // namespace AI::Military
