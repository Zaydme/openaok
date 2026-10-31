#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Nearby.h"

#include <algorithm>
#include <vector>

#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "core/Random.h"
#include "ecs/Animals.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Missiles.h"
#include "ecs/Navigation.h"
#include "ecs/Orders.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Search.h"
#include "ecs/VillagerJobs.h"
#include "ecs/Turning.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/ApproachPlanner.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::Combat {

namespace {

namespace R = Core::Rules;
using Components::Combat;
using Components::CombatState;

// The dat's combat level for military units: what a unit counts as when
// choosing targets.
constexpr int kMilitaryLevel = 4;

// The slack before a unit in a fight decides its target has slipped away:
// enough that a target drifting on the spot does not make its attacker start
// and stop.
const Core::Fixed32 kDisengageSlack = Core::Fixed32(3) / Core::Fixed32(10);

// How often a chase re-plans its route, and how often an idle unit looks for
// a fight. Staggered per entity so a crowd does not all scan in one tick.
constexpr int32_t kChaseReplanMs = 500;
// Empty routes to a target before the chase is given up.
constexpr int kChaseGiveUp = 3;
// The chases planned in one pass at most: an army ordered at once plans its
// routes over a few passes rather than all in one.
constexpr int kChasePlansPerPass = 8;
// A chase still walking, further than kChaseNear from its target, is planned
// again only once the target has strayed kChaseDrift tiles from where the
// walk is going -- at a building, never. Nearer, it is planned again as
// often as ever, to find its place round the target among the others.
const Core::Fixed32 kChaseDrift(1);
const Core::Fixed32 kChaseNear(5);
constexpr int32_t kScanMs = 500;

// A defensive unit pursues no further from where it was than this many times
// its search radius before turning back.
constexpr int kDefensiveLeash = 2;

// A wild predator looks round itself every three quarters to five quarters of
// this, and that of the shorter one after giving a chase up.
constexpr int32_t kHuntMs = 6000;
constexpr int32_t kHuntAgainMs = 1000;
// The lobby's difficulties at which a predator lets a villager come nearer
// before it goes for it (0 is the hardest).
constexpr int kEasy = 3;
constexpr int kEasiest = 4;
// The dat class of villagers, a predator's first choice; and of predators.
constexpr int kVillagerClass = 4;
constexpr int kPreyClass = 9;
constexpr int kPredatorClass = 10;

Core::PlayerId OwnerOf(const entt::registry& registry, entt::entity e) {
    const auto* owner = registry.try_get<Components::Owner>(e);
    return owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
}

// A unit's body radius, from the dat's size.
Core::Fixed32 Radius(const entt::registry& registry, entt::entity e) {
    const R::UnitRules* rules = DefendingRules(registry, e);
    return rules != nullptr ? rules->size_x : Core::Fixed32(0);
}

bool IsBuilding(const entt::registry& registry, entt::entity e) {
    return registry.all_of<Components::Building>(e);
}

// Whether these rules fight at range: a missile of their own, or -- a Town
// Center -- only the one its garrison looses.
bool Shoots(const R::UnitRules& rules) {
    return rules.projectile >= 0 || rules.secondary_projectile >= 0;
}

// How far past touching a melee blow reaches: the dat's work range of the
// unit's fighting tasks -- a soldier's combat task 0, a villager's hunt 0.1.
Core::Fixed32 MeleeReach(const R::UnitRules& rules) {
    Core::Fixed32 reach(0);
    for (const auto& task : rules.tasks) {
        if (task.action == R::TaskAction::Combat || task.action == R::TaskAction::Hunt) {
            reach = Core::FixedMath::Max(reach, task.work_range);
        }
    }
    return reach;
}

// How far, edge to edge, these rules strike from.
Core::Fixed32 Reach(const R::UnitRules& rules) {
    return Shoots(rules) && rules.max_range.value > 0 ? rules.max_range
                                                       : rules.max_range + MeleeReach(rules);
}

// Whether the target is within reach of these rules, by gap.
bool InReach(const R::UnitRules& rules, Core::Fixed32 gap, Core::Fixed32 slack) {
    if (Shoots(rules) && rules.max_range.value > 0) {
        return gap <= rules.max_range + slack && gap >= rules.min_range;
    }
    return gap <= Reach(rules) + slack;
}

// Whether a unit looks for fights of its own: military units and armed
// buildings do; villagers, sheep and boar only answer one.
bool SeeksFights(const R::UnitRules& rules, bool building) {
    if (rules.attack_count == 0) return false;
    if (building) return Shoots(rules);
    return rules.combat_level == kMilitaryLevel;
}

// How far it looks: a unit its search radius, a building its range.
// Two things' extents beyond their middles, together, at most: the dat's
// largest footprint and largest body each reach four tiles, with a tile to spare.
const Core::Fixed32 kFarthestExtents = Core::Fixed32(9);

Core::Fixed32 SearchReach(const R::UnitRules& rules, bool building) {
    return building ? rules.max_range : rules.search_radius;
}

// Whether `self` could strike `other` where it stands. One that shoots
// reaches anything in range; one that fights hand to hand only what stands on,
// or beside, ground of its own layer -- a swordsman does not set off after a
// galley out at sea, nor a demolition ship after a villager inland -- and a
// building whose walls touch it.
bool CanReach(const entt::registry& registry, const Physics::Grid& grid, entt::entity self,
              entt::entity other, const R::UnitRules& rules) {
    if (Shoots(rules)) return true;
    const Physics::Layer layer = Navigation::LayerOf(registry, grid, self);
    const auto& at = registry.get<Components::Transform>(other).position;
    int lo_x = Physics::TileOf(at.x), hi_x = lo_x, lo_y = Physics::TileOf(at.y), hi_y = lo_y;
    if (IsBuilding(registry, other) && registry.all_of<Components::Identity>(other)) {
        const auto ground =
            Buildings::FootprintBounds(at, registry.get<Components::Identity>(other).unit_id);
        lo_x = Physics::TileOf(ground.min_x);
        hi_x = Physics::TileOf(ground.max_x);
        lo_y = Physics::TileOf(ground.min_y);
        hi_y = Physics::TileOf(ground.max_y);
    }
    for (int y = lo_y - 1; y <= hi_y + 1; ++y) {
        for (int x = lo_x - 1; x <= hi_x + 1; ++x) {
            if (grid.Passable(layer, x, y)) return true;
        }
    }
    return false;
}

// The best target in reach for `self`, among the classes it looks for
// (ecs/Search): the highest combat level first, then the nearest, then the
// lowest entity id, so the choice is the same on every machine.
// `candidates` holds everything with a place, health and an owner, by where it
// stands.
entt::entity FindTarget(const entt::registry& registry, const Physics::Grid& grid,
                        entt::entity self, Core::Fixed32 reach, const R::UnitRules& own,
                        const Nearby::Buckets& candidates) {
    entt::entity best = entt::null;
    int best_level = -1;
    Core::Fixed32 best_gap{};
    const uint64_t sought = Search::Sought(registry, self);
    // Nothing in reach is farther from `self`'s middle, on either axis, than
    // the reach and both sides' extents: a footprint or a body reaches at most
    // four tiles from its middle.
    const auto& here = registry.get<Components::Transform>(self).position;
    const Core::Fixed32 box = reach + kFarthestExtents;
    Nearby::ForEachNear(candidates, here, box, [&](entt::entity other) {
        const auto& there = registry.get<Components::Transform>(other).position;
        if (Core::FixedMath::Abs(there.x - here.x) > box || Core::FixedMath::Abs(there.y - here.y) > box) {
            return;
        }
        if (other == self || !Alive(registry, other)) return;
        if (!Hostile(registry, self, other)) return;
        const R::UnitRules* rules = DefendingRules(registry, other);
        if (rules == nullptr || !Search::Holds(sought, rules->cls)) return;
        // A foundation not yet begun is nobody's target.
        if (!Buildings::Begun(registry, other)) return;
        const Core::Fixed32 gap = Gap(registry, self, other);
        if (gap > reach) return;
        if (!CanReach(registry, grid, self, other, own)) return;
        const int level = rules->combat_level;
        const bool better =
            best == entt::null || level > best_level ||
            (level == best_level &&
             (gap < best_gap || (gap == best_gap && other < best)));
        if (better) {
            best = other;
            best_level = level;
            best_gap = gap;
        }
    });
    return best;
}

// A predator's wait before it next looks round: `period` give or take a
// quarter, from the world's own numbers.
int32_t HuntWait(entt::registry& registry, int32_t period) {
    auto* dice = registry.ctx().find<HuntRandom>();
    if (dice == nullptr) dice = &registry.ctx().emplace<HuntRandom>();
    auto& rng = dice->rng;
    return period * 3 / 4 + static_cast<int32_t>(rng.NextInt(static_cast<uint64_t>(period / 2)));
}

// What a wild predator goes for when it looks round. It sees as far as its
// line of sight, and goes first for the nearest villager there: at Moderate
// or harder wherever it sees one, at Easy once it is within half its sight,
// at Easiest within a third. A villager seen further off is waited for, not
// passed over for something nearer. With no villager in sight it goes for
// the nearest of anything else it hunts once that is within a third of its
// sight.
entt::entity FindPrey(const entt::registry& registry, entt::entity self, const R::UnitRules& own,
                      const Nearby::Buckets& candidates) {
    const uint64_t sought = Search::Sought(registry, self);
    const auto& here = registry.get<Components::Transform>(self).position;
    const Core::Fixed32 sight = own.los;
    entt::entity villager = entt::null, other = entt::null;
    Core::Fixed32 villager_at{}, other_at{};
    Nearby::ForEachNear(candidates, here, sight, [&](entt::entity e) {
        if (e == self || !Alive(registry, e) || !Hostile(registry, self, e)) return;
        const R::UnitRules* rules = DefendingRules(registry, e);
        if (rules == nullptr || !Search::Holds(sought, rules->cls)) return;
        const auto& there = registry.get<Components::Transform>(e).position;
        const Core::Fixed32 apart = Core::FixedMath::Distance(there.x - here.x, there.y - here.y);
        if (apart > sight) return;
        entt::entity& best = rules->cls == kVillagerClass ? villager : other;
        Core::Fixed32& best_at = rules->cls == kVillagerClass ? villager_at : other_at;
        if (best == entt::null || apart < best_at || (apart == best_at && e < best)) {
            best = e;
            best_at = apart;
        }
    });
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const int difficulty = setup != nullptr ? setup->difficulty : 0;
    if (villager != entt::null) {
        if (difficulty == kEasy) return Gap(registry, self, villager) <= sight / Core::Fixed32(2) ? villager : entt::null;
        if (difficulty == kEasiest) return Gap(registry, self, villager) <= sight / Core::Fixed32(3) ? villager : entt::null;
        return villager;
    }
    if (other != entt::null && Gap(registry, self, other) <= sight / Core::Fixed32(3)) return other;
    return entt::null;
}

bool IsVillager(const entt::registry& registry, entt::entity e) {
    const auto* worker = registry.try_get<Components::Worker>(e);
    return worker != nullptr && worker->gatherer < 0;
}

// Those who never come to help: relic carts, trade and fishing boats,
// buildings, walls and gates, Monks, trade carts and transports.
bool NeverHelps(int cls) {
    switch (cls) {
    case 1: case 2: case 3: case 18: case 19: case 20: case 21: case 27: case 39:
        return true;
    default:
        return false;
    }
}

// Computer players' units set on since the last pass (Warn): the one set on,
// and by whom.
struct Warnings {
    std::vector<std::pair<entt::entity, entt::entity>> list;
};

// How near the struck one those who come to its help stand, edge to edge, in
// tiles.
const Core::Fixed32 kHelpReach = Core::Fixed32(3);
// The farthest an aggressive unit comes to help from: its line of sight, up
// to this many tiles.
const Core::Fixed32 kAggressiveHelpMost = Core::Fixed32(12);
// A relic, and the relic cart: nothing a villager takes on.
constexpr int kArtifactClass = 1;
constexpr int kRelicClass = 42;
// How far a villager runs from another player's attacker: this many times
// what it walks in a second.
constexpr int kRunSeconds = 2;
// Places tried about where it would run to when that ground is closed, and
// how far about it, in tiles.
constexpr int kRunTries = 5;
constexpr int kRunScatter = 3;

// Help for `victim`, struck by `attacker`: every idle unit of its side or of
// an ally of its, within three tiles of it, that would take the attacker on
// and can get at it, goes for it. Villagers come only against Gaia's.
void CallForHelp(entt::registry& registry, const Physics::Grid& grid, entt::entity victim,
                 entt::entity attacker) {
    const R::UnitRules* struck_by = DefendingRules(registry, attacker);
    if (struck_by == nullptr) return;
    const bool gaia = OwnerOf(registry, attacker) == Core::PLAYER_GAIA;
    const auto& here = registry.get<Components::Transform>(victim).position;
    const Core::Fixed32 near = kAggressiveHelpMost + kFarthestExtents;
    std::vector<entt::entity> helpers;
    auto fighters = registry.view<Combat, Components::Transform, Components::Owner>();
    for (auto e : fighters) {
        // Where it stands first: nearly every fighter is elsewhere.
        const auto& at = fighters.get<Components::Transform>(e).position;
        if (Core::FixedMath::Abs(at.x - here.x) > near || Core::FixedMath::Abs(at.y - here.y) > near) continue;
        if (e == victim || e == attacker || !Alive(registry, e) || IsBuilding(registry, e)) continue;
        if (!Friendly(registry, victim, e) || !Friendly(registry, e, victim)) continue;
        const auto& combat = fighters.get<Combat>(e);
        if (combat.state != CombatState::Idle || combat.stance == Components::Stance::NoAttack) continue;
        if (const auto* path = registry.try_get<Components::Path>(e); path != nullptr && path->has_goal) continue;
        if (const auto* worker = registry.try_get<Components::Worker>(e);
            worker != nullptr && worker->state != Core::UnitState::Idle) {
            continue;
        }
        const R::UnitRules* rules = DefendingRules(registry, e);
        if (rules == nullptr || NeverHelps(rules->cls)) continue;
        // Whether it would take the attacker on: a villager anything of
        // Gaia's but a relic; anything else, of Gaia's, only a predator or
        // prey, and of a player's what it looks for.
        if (IsVillager(registry, e)) {
            if (!gaia || struck_by->cls == kArtifactClass || struck_by->cls == kRelicClass) continue;
        } else if (gaia ? struck_by->cls != kPredatorClass && struck_by->cls != kPreyClass
                        : !Search::Holds(Search::Sought(registry, e), struck_by->cls)) {
            continue;
        }
        // An aggressive one comes from as far as it sees.
        const Core::Fixed32 reach = combat.stance == Components::Stance::Aggressive
                                        ? Core::FixedMath::Max(kHelpReach, Core::FixedMath::Min(rules->los, kAggressiveHelpMost))
                                        : kHelpReach;
        if (Gap(registry, e, victim) > reach) continue;
        const R::UnitRules* own = FightingRules(registry, e);
        if (own == nullptr || !CanReach(registry, grid, e, attacker, *own)) continue;
        helpers.push_back(e);
    }
    for (entt::entity e : helpers) Engage(registry, e, attacker, false);
}

// A villager runs from another player's attacker: twice what it walks in a
// second, straight away from it -- any way, standing on it -- or about there
// when that ground is closed.
void RunFrom(entt::registry& registry, const Physics::Grid& grid, entt::entity villager,
             entt::entity attacker) {
    const R::UnitRules* rules = FightingRules(registry, villager);
    if (rules == nullptr || rules->speed.value <= 0) return;
    auto& rng = registry.ctx().get<Core::Random>();
    const auto& at = registry.get<Components::Transform>(villager).position;
    const auto& from = registry.get<Components::Transform>(attacker).position;
    Core::Fixed32 dx = at.x - from.x, dy = at.y - from.y;
    Core::Fixed32 apart = Core::FixedMath::Distance(dx, dy);
    if (apart < Core::Fixed32(1) / Core::Fixed32(100)) {
        dx = Core::Fixed32(static_cast<int>(rng.NextInt(201)) - 100) / Core::Fixed32(100);
        dy = Core::Fixed32(static_cast<int>(rng.NextInt(201)) - 100) / Core::Fixed32(100);
        apart = Core::FixedMath::Max(Core::FixedMath::Distance(dx, dy), Core::Fixed32(1) / Core::Fixed32(100));
    }
    const Core::Fixed32 run = rules->speed * Core::Fixed32(kRunSeconds);
    const Core::Fixed32 last_x(grid.GetWidth() - 1), last_y(grid.GetHeight() - 1);
    auto clamp = [&](Components::Vector2D p) {
        p.x = Core::FixedMath::Max(Core::Fixed32(0), Core::FixedMath::Min(last_x, p.x));
        p.y = Core::FixedMath::Max(Core::Fixed32(0), Core::FixedMath::Min(last_y, p.y));
        return p;
    };
    Components::Vector2D to = clamp({at.x + dx * run / apart, at.y + dy * run / apart});
    for (int i = 0; i < kRunTries && !Navigation::CanStand(registry, grid, villager, to); ++i) {
        const int span = 2 * kRunScatter + 1;
        to = clamp({to.x + Core::Fixed32(static_cast<int>(rng.NextInt(span)) - kRunScatter),
                    to.y + Core::Fixed32(static_cast<int>(rng.NextInt(span)) - kRunScatter)});
    }
    registry.emplace_or_replace<Components::Path>(
        villager, Navigation::Route(registry, grid, villager, at, Navigation::Toward(registry, grid, villager, to)));
}

// A villager struck: it fights off Gaia's attacker -- a wolf -- and runs from
// anyone else's, breaking off its job and going back to it after. It keeps at
// what it is doing walking where it was sent, repairing, fighting already, or
// hunting an animal that fights back -- a boar -- and is struck by it.
void AnswerAsVillager(entt::registry& registry, const Physics::Grid& grid, entt::entity villager,
                      entt::entity attacker) {
    auto* worker = registry.try_get<Components::Worker>(villager);
    auto* combat = registry.try_get<Combat>(villager);
    if (worker == nullptr || combat == nullptr) return;
    if (worker->state == Core::UnitState::Repairing) return;
    const bool fighting = combat->state != CombatState::Idle && Alive(registry, combat->target);
    const bool hunting = fighting && worker->hunted == combat->target;
    if (fighting && !hunting) return;
    const bool gaia = OwnerOf(registry, attacker) == Core::PLAYER_GAIA;
    if (hunting && gaia) {
        const R::UnitRules* quarry = DefendingRules(registry, combat->target);
        if (quarry != nullptr && quarry->cls == kPredatorClass) return;
    }
    const auto* path = registry.try_get<Components::Path>(villager);
    const bool walking = path != nullptr && path->has_goal;
    const bool sent = walking && (worker->state == Core::UnitState::Moving ||
                                  registry.all_of<Components::Boarding>(villager));
    if (gaia) {
        if (sent) return;
        Orders::Interrupt(registry, villager);
        Engage(registry, villager, attacker, false);
        return;
    }
    if (walking) return;
    Orders::Interrupt(registry, villager);
    RunFrom(registry, grid, villager, attacker);
}

void Disengage(entt::registry& registry, const Physics::Grid& grid, entt::entity self,
               Combat& combat) {
    combat.target = entt::null;
    combat.ordered = false;
    combat.state = CombatState::Idle;
    combat.strike_ms = -1;
    // A defensive unit goes back to where it was.
    if (combat.stance == Components::Stance::Defensive && combat.has_post &&
        !registry.all_of<Components::Building>(self)) {
        const auto& at = registry.get<Components::Transform>(self).position;
        if (Core::FixedMath::Distance(at.x - combat.post.x, at.y - combat.post.y) >
            Core::Fixed32(1)) {
            auto back = Navigation::Route(registry, grid, self, at, combat.post);
            if (!back.waypoints.empty()) {
                registry.emplace_or_replace<Components::Path>(self, std::move(back));
            }
        }
    }
}

// Re-plans a chase toward the target: straight at a unit, or to a spot
// against a building's wall. False when there is no route to it at all.
// Whether a chase walking on, still far from its target, goes where the
// target is: a building, which never moves, or a unit within kChaseDrift of
// the walk's goal.
bool StillThere(const entt::registry& registry, entt::entity self, entt::entity target,
                const Components::Path& path) {
    if (path.target != target || !path.has_goal) return false;
    if (Gap(registry, self, target) <= kChaseNear) return false;
    if (IsBuilding(registry, target)) return true;
    const auto& at = registry.get<Components::Transform>(target).position;
    return Core::FixedMath::Distance(at.x - path.goal.x, at.y - path.goal.y) <= kChaseDrift;
}

// Within this many tiles of a unit it fights hand to hand, an attacker makes
// for a place of its own about it rather than for its middle.
const Core::Fixed32 kSurroundNear = Core::Fixed32(3);

// Where `self` stands to fight `target` hand to hand beside the others on it:
// a place round the target's body clear of those striking it and of where
// those coming at it are headed, or, every place taken, standing by a body's
// width further out. False when there is no such place.
// Where those also set on `target` stand, or, on their way, are headed.
void SetOnIt(const entt::registry& registry, entt::entity self, entt::entity target,
             std::vector<Components::Vector2D>& taken) {
    taken.clear();
    auto fighters = registry.view<const Combat, const Components::Transform>();
    for (auto e : fighters) {
        if (e == self) continue;
        const auto& combat = fighters.get<const Combat>(e);
        if (combat.target != target || combat.state == CombatState::Idle) continue;
        const auto* path = registry.try_get<Components::Path>(e);
        if (combat.state == CombatState::Chasing && path != nullptr && path->has_goal &&
            path->current_waypoint_index < path->waypoints.size()) {
            taken.push_back(path->goal);
        } else {
            taken.push_back(fighters.get<const Components::Transform>(e).position);
        }
    }
}

bool SurroundSpot(const entt::registry& registry, const Physics::Grid& grid, entt::entity self,
                  entt::entity target, const R::UnitRules& rules, Components::Vector2D& spot) {
    static std::vector<Components::Vector2D> taken;
    SetOnIt(registry, self, target, taken);
    const Core::Fixed32 radius = Radius(registry, self);
    Pathfinding::ApproachRequest ask;
    ask.target = BoxOf(registry, target);
    ask.from = registry.get<Components::Transform>(self).position;
    ask.standoff = radius + Reach(rules) / Core::Fixed32(2);
    ask.clearance = radius + radius;
    ask.taken = taken.empty() ? nullptr : taken.data();
    ask.taken_count = taken.size();
    ask.layer = Navigation::LayerOf(registry, grid, self);
    auto found = Pathfinding::FindStandingSpot(grid, ask);
    if (!found.found && found.full) {
        ask.standoff = ask.standoff + ask.clearance;
        found = Pathfinding::FindStandingSpot(grid, ask);
    }
    if (!found.found) return false;
    spot = found.spot;
    return true;
}

bool PlanChase(entt::registry& registry, const Physics::Grid& grid, entt::entity self,
               entt::entity target, const R::UnitRules& rules) {
    const auto& from = registry.get<Components::Transform>(self).position;
    const auto& at = registry.get<Components::Transform>(target).position;
    const Physics::Layer layer = Navigation::LayerOf(registry, grid, self);
    Components::Vector2D goal = at;
    // Near a unit it fights hand to hand: to a place of its own about it.
    const bool building_target = IsBuilding(registry, target);
    if (!Shoots(rules) && !building_target && Gap(registry, self, target) <= kSurroundNear &&
        Navigation::CanStand(registry, grid, self, at)) {
        Components::Vector2D spot{};
        if (SurroundSpot(registry, grid, self, target, rules, spot)) {
            auto path = Navigation::Route(registry, grid, self, from, spot, target, Reach(rules));
            if (!path.waypoints.empty()) {
                registry.emplace_or_replace<Components::Path>(self, std::move(path));
                return true;
            }
        }
    }
    // A building is reached at its wall; so is a unit standing where this one
    // cannot go -- a galley shooting at the shore, an archer at a ship -- from
    // the nearest of its own ground within reach.
    const bool building = IsBuilding(registry, target) &&
                          registry.all_of<Components::Identity>(target);
    if (building || !Navigation::CanStand(registry, grid, self, at)) {
        Pathfinding::ApproachRequest ask;
        ask.target = building ? Buildings::FootprintBounds(
                                    at, registry.get<Components::Identity>(target).unit_id)
                              : Buildings::TileBounds(at);
        ask.from = from;
        const Core::Fixed32 reach = Reach(rules);
        const Core::Fixed32 radius = Radius(registry, self);
        ask.standoff = radius + reach / Core::Fixed32(2);
        ask.clearance = Core::FixedMath::Max(Core::Fixed32(1) / Core::Fixed32(2), radius + radius);
        ask.layer = layer;
        // Clear of the others at it, near it: a crowd at a wall spreads
        // along it rather than all making for the one nearest spot.
        static std::vector<Components::Vector2D> taken;
        taken.clear();
        if (Gap(registry, self, target) <= kSurroundNear + reach) SetOnIt(registry, self, target, taken);
        ask.taken = taken.empty() ? nullptr : taken.data();
        ask.taken_count = taken.size();
        auto spot = Pathfinding::FindStandingSpot(grid, ask);
        if (!spot.found && spot.full) {
            ask.standoff = ask.standoff + ask.clearance;
            spot = Pathfinding::FindStandingSpot(grid, ask);
        }
        if (spot.found) goal = spot.spot;
    }
    // The walk ends once the target is within reach, wherever it has got to.
    auto path = Navigation::Route(registry, grid, self, from, goal, target, Reach(rules));
    if (path.waypoints.empty()) return false;
    registry.emplace_or_replace<Components::Path>(self, std::move(path));
    return true;
}

// Starts one attack: the attack animation from its first frame, the blow or
// the loosing landing on the dat's frame delay into it, and the reload.
void StartSwing(entt::registry& registry, entt::entity self, Combat& combat,
                const R::UnitRules& rules) {
    combat.reload_ms = std::max<int32_t>(1, (rules.reload * Core::Fixed32(1000)).ToInt());
    combat.strike_ms = 0;
    combat.swing_ms = 0;
    if (auto* anim = registry.try_get<Components::Animation>(self)) {
        const int attack = static_cast<int>(Components::AnimState::Attack);
        if (anim->slp[attack] >= 0 && anim->frames[attack] > 0) {
            combat.strike_ms = rules.frame_delay * anim->frame_ms[attack];
            combat.swing_ms = anim->frames[attack] * anim->frame_ms[attack];
            anim->needs_apply = true;   // from the first frame
        }
    }
}

struct Strike {
    entt::entity target;
    Core::Fixed32 amount;
    entt::entity attacker;
};

// The dat's blast levels: 3 hurts only the target; below it a blow or a
// missile hurts what stands near the target as well.
constexpr int kBlastTargetOnly = 3;

// A blow that blasts -- a demolition ship's, a ram's -- hurts every unit and
// building within the attacker's blast width of its target as well, each by
// the attacker's own attack against its armour -- all but its own side and
// its allies, and of Gaia's only what hunts. A mangonel's stone, a missile,
// is ecs/Missiles'.
void MeleeBlast(const entt::registry& registry, const Physics::Grid& grid, entt::entity self,
                entt::entity target, const R::UnitRules& rules, std::vector<Strike>& blows) {
    if (rules.blast_width.value <= 0 || rules.blast_level < 0 ||
        rules.blast_level >= kBlastTargetOnly) {
        return;
    }
    const auto& centre = registry.get<Components::Transform>(target).position;
    auto all = registry.view<const Components::Transform, const Components::Health>();
    for (auto other : all) {
        if (other == self || other == target || !Alive(registry, other)) continue;
        if (Friendly(registry, self, other)) continue;
        if (OwnerOf(registry, other) == Core::PLAYER_GAIA && !Hostile(registry, self, other)) {
            continue;
        }
        const auto& at = all.get<const Components::Transform>(other).position;
        Core::Fixed32 apart{};
        if (IsBuilding(registry, other) && registry.all_of<Components::Identity>(other)) {
            apart = Buildings::FootprintBounds(at, registry.get<Components::Identity>(other).unit_id)
                        .DistanceFrom(centre);
        } else {
            apart = Core::FixedMath::Distance(at.x - centre.x, at.y - centre.y) -
                    Radius(registry, other);
        }
        if (apart > rules.blast_width) continue;
        const R::UnitRules* defender = DefendingRules(registry, other);
        if (defender == nullptr) continue;
        blows.push_back({other,
                         Blow(Damage(rules.attacks, rules.attack_count, *defender),
                              LevelOf(registry, grid, self), LevelOf(registry, grid, other),
                              false),
                         self});
    }
}

// A unit that is spent in its blow: the demolition ship (string 26424), known
// by its attack -- a warship (class 22) striking hand to hand with a blast. A
// ram's blast leaves the ram standing.
constexpr int kWarshipClass = 22;
bool SpentInBlow(const R::UnitRules& rules) {
    return rules.cls == kWarshipClass && !Shoots(rules) && rules.blast_width.value > 0;
}

} // namespace

void FirstLook(entt::registry& registry, entt::entity predator) {
    auto* combat = registry.try_get<Combat>(predator);
    if (combat != nullptr && Search::Hunts(registry, predator)) combat->scan_ms = HuntWait(registry, kHuntMs);
}

void Face(entt::registry& registry, entt::entity self, entt::entity target) {
    auto* sprite = registry.try_get<Components::Sprite>(self);
    if (sprite == nullptr || registry.all_of<Components::Building>(self)) return;
    const auto* from = registry.try_get<Components::Transform>(self);
    const auto* to = registry.try_get<Components::Transform>(target);
    if (from == nullptr || to == nullptr) return;
    const Core::Fixed32 dx = to->position.x - from->position.x;
    const Core::Fixed32 dy = to->position.y - from->position.y;
    if (dx.value != 0 || dy.value != 0) {
        sprite->heading = Core::FixedMath::WorldVectorToHeading(dx, dy);
    }
}

// Both by the table the entity plays by: its owner's, or a converted unit's
// maker's (Components::RulesOf).
const R::UnitRules* FightingRules(const entt::registry& registry, entt::entity entity) {
    return Players::RulesFor(registry, entity, Villagers::DisplayUnit(registry, entity));
}

const R::UnitRules* DefendingRules(const entt::registry& registry, entt::entity entity) {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    if (identity == nullptr) return nullptr;
    return Players::RulesFor(registry, entity, identity->unit_id);
}

int Damage(const R::ClassAmount* attacks, int attack_count, const R::UnitRules& defender) {
    int total = 0;
    for (int i = 0; i < attack_count; ++i) {
        const int armour = R::ClassValue(defender.armours, defender.armour_count,
                                         attacks[i].cls, defender.base_armour);
        total += std::max(0, attacks[i].amount - armour);
    }
    return std::max(1, total);
}

Core::Fixed32 Blow(int damage, int attacker_level, int target_level, bool stray) {
    Core::Fixed32 amount(damage);
    if (attacker_level > target_level) amount = amount * Core::Fixed32(5) / Core::Fixed32(4);
    if (attacker_level < target_level) amount = amount * Core::Fixed32(3) / Core::Fixed32(4);
    if (amount < Core::Fixed32(1)) amount = Core::Fixed32(1);
    if (stray) amount = amount / Core::Fixed32(2);
    return amount;
}

int HeightAdjusted(int damage, int attacker_level, int target_level) {
    return Blow(damage, attacker_level, target_level, false).ToInt();
}

int LevelOf(const entt::registry& registry, const Physics::Grid& grid, entt::entity entity) {
    const auto* at = registry.try_get<Components::Transform>(entity);
    if (at == nullptr) return 0;
    return grid.GetTile(Physics::TileOf(at->position.x), Physics::TileOf(at->position.y))
        .elevation;
}

bool Alive(const entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || registry.all_of<Components::Dying>(entity)) return false;
    // A unit sheltering in a building is out of the fight until it comes out.
    if (!registry.all_of<Components::Transform>(entity)) return false;
    const auto* health = registry.try_get<Components::Health>(entity);
    return health != nullptr && health->current_hp > 0;
}

bool Hostile(const entt::registry& registry, entt::entity a, entt::entity b) {
    const Core::PlayerId pa = OwnerOf(registry, a);
    const Core::PlayerId pb = OwnerOf(registry, b);
    if (pa == pb) return false;
    // Gaia's side: only what hunts is an enemy, and it is everyone's.
    auto hunts = [&](entt::entity e) {
        const R::UnitRules* rules = DefendingRules(registry, e);
        return rules != nullptr && rules->attack_count > 0 &&
               rules->combat_level == kMilitaryLevel &&
               registry.all_of<Components::Combat>(e);
    };
    if (pa == Core::PLAYER_GAIA) return hunts(a);
    if (pb == Core::PLAYER_GAIA) return hunts(b);
    return Diplomacy::StanceOf(registry, pa, pb) == Diplomacy::Stance::Enemy;
}

bool Friendly(const entt::registry& registry, entt::entity a, entt::entity b) {
    const Core::PlayerId pa = OwnerOf(registry, a);
    const Core::PlayerId pb = OwnerOf(registry, b);
    return pa == pb || Diplomacy::StanceOf(registry, pa, pb) == Diplomacy::Stance::Ally;
}

Buildings::Bounds BoxOf(const entt::registry& registry, entt::entity entity) {
    const auto& at = registry.get<Components::Transform>(entity).position;
    if (IsBuilding(registry, entity) && registry.all_of<Components::Identity>(entity)) {
        return Buildings::FootprintBounds(at, registry.get<Components::Identity>(entity).unit_id);
    }
    // The dat's axes are a quarter turn from the grid's: its y extent is the
    // grid's x.
    const R::UnitRules* rules = DefendingRules(registry, entity);
    const Core::Fixed32 half_x = rules != nullptr ? rules->size_y : Core::Fixed32(0);
    const Core::Fixed32 half_y = rules != nullptr ? rules->size_x : Core::Fixed32(0);
    return {at.x - half_x, at.y - half_y, at.x + half_x, at.y + half_y};
}

Core::Fixed32 Gap(const entt::registry& registry, entt::entity from, entt::entity to) {
    const Buildings::Bounds a = BoxOf(registry, from);
    const Buildings::Bounds b = BoxOf(registry, to);
    const Core::Fixed32 zero(0);
    const Core::Fixed32 across = Core::FixedMath::Max(zero, Core::FixedMath::Max(a.min_x - b.max_x, b.min_x - a.max_x));
    const Core::Fixed32 down = Core::FixedMath::Max(zero, Core::FixedMath::Max(a.min_y - b.max_y, b.min_y - a.max_y));
    return Core::FixedMath::Distance(across, down);
}

namespace {

// The longest a fresh fight's first blow or shot waits, in milliseconds, so
// those that come into reach together do not all strike in one tick.
constexpr int32_t kFirstSwingSpreadMs = 600;

// `attacker`'s wait before its first blow at a new target: its own share of
// kFirstSwingSpreadMs, from who it is and when, the same on every machine.
int32_t FirstSwingWait(const entt::registry& registry, entt::entity attacker) {
    const Events::Log* log = Events::Read(registry);
    const uint64_t tick = log != nullptr ? log->tick : 0;
    uint64_t h = static_cast<uint64_t>(entt::to_integral(attacker)) * 0x9E3779B97F4A7C15ull;
    h ^= tick * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    return static_cast<int32_t>(h % static_cast<uint64_t>(kFirstSwingSpreadMs));
}

} // namespace

void Engage(entt::registry& registry, entt::entity attacker, entt::entity target,
            bool ordered) {
    auto* combat = registry.try_get<Combat>(attacker);
    if (combat == nullptr || attacker == target || !registry.valid(target)) return;
    if (!combat->has_post || combat->state == CombatState::Idle) {
        combat->post = registry.get<Components::Transform>(attacker).position;
        combat->has_post = true;
    }
    if (combat->target != target) combat->reload_ms = std::max(combat->reload_ms, FirstSwingWait(registry, attacker));
    combat->target = target;
    combat->ordered = ordered;
    combat->chase_ms = 0;
    combat->strike_ms = -1;
    // Already in reach: straight to it, without a tick of chasing in which
    // the crowd could shove the pair apart.
    const R::UnitRules* rules = FightingRules(registry, attacker);
    combat->state = rules != nullptr && InReach(*rules, Gap(registry, attacker, target),
                                                Core::Fixed32(0))
                  ? CombatState::Attacking : CombatState::Chasing;
    if (combat->state == CombatState::Attacking) registry.remove<Components::Path>(attacker);
    // A computer player's unit is told it has been set on (Warn), in the
    // next pass.
    const auto* setup = registry.ctx().find<Core::GameSetup>();
    const Core::PlayerId own = OwnerOf(registry, target);
    if (setup != nullptr && own != Core::PLAYER_GAIA && own < Core::kMaxPlayers && setup->players[own].computer &&
        !Friendly(registry, target, attacker)) {
        registry.ctx().emplace<Warnings>().list.emplace_back(target, attacker);
    }
}

// What `target` does about `attacker` setting on it or striking it.
void Answer(entt::registry& registry, Physics::Grid& grid, entt::entity target, entt::entity attacker) {
    // Answering an attack, never from its own side nor one its owner regards
    // as an ally. A new attacker first brings help from those idle nearby. A
    // villager fights off Gaia's and runs from anyone else's (AnswerAsVillager);
    // anything else armed and not told otherwise turns on whoever hurt it -- a
    // boar on its hunter, a soldier on a neutral's -- unless busy at a job or
    // at a building a person sent it at.
    if (Alive(registry, target) && Alive(registry, attacker) &&
        !Friendly(registry, target, attacker)) {
        if (auto* combat = registry.try_get<Combat>(target);
            combat != nullptr && combat->struck_by != attacker && combat->target != attacker) {
            combat->struck_by = attacker;
            CallForHelp(registry, grid, target, attacker);
        }
        if (IsVillager(registry, target)) {
            AnswerAsVillager(registry, grid, target, attacker);
        } else if (auto* combat = registry.try_get<Combat>(target)) {
            const auto* worker = registry.try_get<Components::Worker>(target);
            // Running for cover is busy too: it keeps running.
            const bool busy = (worker != nullptr && worker->state != Core::UnitState::Idle) ||
                              registry.all_of<Components::Boarding>(target);
            // At a building nobody sent it at -- a wall it came upon -- it
            // turns on whoever strikes it; at one a person sent it at, it
            // keeps on. A computer player's turns on its striker either way.
            const auto* setup = registry.ctx().find<Core::GameSetup>();
            const Core::PlayerId own = OwnerOf(registry, target);
            const bool computer = setup != nullptr && own < Core::kMaxPlayers && setup->players[own].computer;
            const bool unsent = combat->state != CombatState::Idle && (!combat->ordered || computer) &&
                                IsBuilding(registry, combat->target);
            if ((combat->state == CombatState::Idle || unsent) && !busy &&
                combat->stance != Components::Stance::NoAttack &&
                !IsBuilding(registry, target)) {
                Engage(registry, target, attacker, false);
            }
        }
    }
}

// A computer player's unit told an attacker has set on it: with its player's
// ability-to-dodge-missiles percent it does not answer a ranged one before
// it is struck, else it answers now -- the news goes to its player as a
// blow would; and a ranged soldier of its, itself attacking something other
// than a building, backs off from the attacker to its own range, but for
// the player's ability-to-maintain-distance percent.
void Warn(entt::registry& registry, Physics::Grid& grid, entt::entity target, entt::entity attacker) {
    if (!Alive(registry, target) || !Alive(registry, attacker)) return;
    const auto* combat = registry.try_get<Combat>(attacker);
    if (combat == nullptr || combat->target != target) return;
    const Players::Player* player = Players::Find(registry, OwnerOf(registry, target));
    const R::UnitRules* own = FightingRules(registry, target);
    const R::UnitRules* theirs = FightingRules(registry, attacker);
    if (player == nullptr || own == nullptr || theirs == nullptr) return;
    auto& rng = registry.ctx().get<Core::Random>();
    if (theirs->max_range > Core::Fixed32(1) && static_cast<int>(rng.NextInt(100)) >= player->dodge) {
        const auto& at = registry.get<Components::Transform>(target).position;
        Events::Emit(registry, {.kind = Events::Kind::Struck, .subject = target, .other = attacker,
                                .player = OwnerOf(registry, target), .at = at});
        Answer(registry, grid, target, attacker);
    }
    auto* mine = registry.try_get<Combat>(target);
    if (mine == nullptr || IsVillager(registry, target) || IsBuilding(registry, target)) return;
    if (own->max_range <= Core::Fixed32(1) || mine->state == CombatState::Idle || IsBuilding(registry, mine->target)) return;
    if (static_cast<int>(rng.NextInt(100)) < player->keep_distance) return;
    const auto& here = registry.get<Components::Transform>(target).position;
    const auto& there = registry.get<Components::Transform>(attacker).position;
    const Core::Fixed32 dx = here.x - there.x, dy = here.y - there.y;
    const Core::Fixed32 apart = Core::FixedMath::Distance(dx, dy);
    if (apart.value <= 0 || apart >= own->max_range) return;
    const Components::Vector2D to{there.x + dx * own->max_range / apart, there.y + dy * own->max_range / apart};
    registry.emplace_or_replace<Components::Path>(
        target, Navigation::Route(registry, grid, target, here, Navigation::Toward(registry, grid, target, to)));
}

void Hurt(entt::registry& registry, Physics::Grid& grid, entt::entity target,
          Core::Fixed32 amount, entt::entity attacker, int attacker_player) {
    if (!Alive(registry, target)) return;
    auto& health = registry.get<Components::Health>(target);
    health.wound = health.wound + amount;
    const int whole = health.wound.ToInt();
    health.wound = health.wound - Core::Fixed32(whole);
    health.current_hp -= whole;

    // News of it, for the owner's alarm.
    if (registry.valid(attacker)) {
        const auto* owner = registry.try_get<Components::Owner>(target);
        const auto* at = registry.try_get<Components::Transform>(target);
        Events::Emit(registry, {.kind = Events::Kind::Struck,
                                .subject = target,
                                .other = attacker,
                                .player = owner != nullptr ? owner->player_id : Core::PLAYER_GAIA,
                                .at = at != nullptr ? at->position : Components::Vector2D{}});
    }

    if (health.current_hp > 0) Answer(registry, grid, target, attacker);
    // Prey runs instead.
    if (health.current_hp > 0 && registry.valid(attacker)) {
        Animals::Flee(registry, grid, target, attacker);
    }
    if (health.current_hp <= 0) {
        // The kill's credit, while the victim is still what it was
        // (ecs/Score).
        if (attacker_player < 0 && registry.valid(attacker)) {
            attacker_player = OwnerOf(registry, attacker);
        }
        Score::Killed(registry, target, attacker_player);
        Mortality::Kill(registry, grid, target);
    }
}

void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms) {
    if (auto* warnings = registry.ctx().find<Warnings>(); warnings != nullptr && !warnings->list.empty()) {
        const auto list = std::move(warnings->list);
        warnings->list.clear();
        for (const auto& [target, attacker] : list) Warn(registry, grid, target, attacker);
    }
    std::vector<Strike> blows;
    std::vector<std::pair<entt::entity, entt::entity>> volleys;   // shooter, target
    std::vector<entt::entity> spent;   // demolition ships that have detonated

    // What can be fought, by where it stands; nothing moves during the pass.
    static Nearby::Buckets candidates;
    Nearby::Build(candidates,
                  registry.view<const Components::Transform, const Components::Health, const Components::Owner>(),
                  grid.GetWidth(), grid.GetHeight());

    // What each fighter's own checks read, found once for the pass.
    const auto& dying = registry.storage<Components::Dying>();
    const auto& healths = registry.storage<Components::Health>();
    const auto& buildings = registry.storage<Components::Building>();
    const auto& makers = registry.storage<Components::RulesOf>();
    const auto& owners = registry.storage<Components::Owner>();
    const Players::Table* table = Players::TableOf(registry);

    int plans = 0;
    auto fighters = registry.view<Combat, Components::Transform>();
    for (auto self : fighters) {
        auto& combat = fighters.get<Combat>(self);
        // Alive: the view has its Transform.
        if (dying.contains(self) || !healths.contains(self) || healths.get(self).current_hp <= 0) continue;
        combat.reload_ms = std::max<int32_t>(0, combat.reload_ms - dt_ms);
        combat.swing_ms = std::max<int32_t>(0, combat.swing_ms - dt_ms);

        // FightingRules, by the table it plays by.
        const Core::PlayerId rules_player = makers.contains(self)   ? makers.get(self).player
                                            : owners.contains(self) ? owners.get(self).player_id
                                                                    : Core::PLAYER_GAIA;
        const R::UnitRules* rules = Players::UnitOf(table, rules_player, Villagers::DisplayUnit(registry, self));
        if (rules == nullptr || rules->attack_count == 0) continue;
        const bool building = buildings.contains(self);

        if (combat.state != CombatState::Idle && !Alive(registry, combat.target)) {
            Disengage(registry, grid, self, combat);
            // A predator whose prey has died looks round again at once.
            if (Search::Hunts(registry, self)) combat.scan_ms = 0;
        }
        // Another player's, whom its owner has since made an ally, is left
        // alone, ordered or not: allies cannot be attacked. (Its own are
        // another matter: a shepherd kills its own sheep.)
        if (combat.state != CombatState::Idle &&
            OwnerOf(registry, combat.target) != OwnerOf(registry, self) &&
            Friendly(registry, self, combat.target)) {
            Disengage(registry, grid, self, combat);
        }
        // A building with nothing to loose -- an empty Town Center -- does not
        // fight at all.
        if (building && Missiles::VolleyOf(registry, self).Total() == 0) {
            if (combat.state != CombatState::Idle) Disengage(registry, grid, self, combat);
            continue;
        }

        switch (combat.state) {
        case CombatState::Idle: {
            if (combat.stance == Components::Stance::NoAttack) break;
            if (!SeeksFights(*rules, building)) break;
            combat.scan_ms -= dt_ms;
            if (combat.scan_ms > 0) break;
            if (Search::Hunts(registry, self)) {
                combat.scan_ms = HuntWait(registry, kHuntMs);
                const entt::entity prey = FindPrey(registry, self, *rules, candidates);
                if (prey != entt::null) Engage(registry, self, prey, false);
                break;
            }
            combat.scan_ms = kScanMs + static_cast<int32_t>(entt::to_integral(self) % 8) * 16;
            // Stand ground looks only as far as it can reach.
            const Core::Fixed32 look =
                combat.stance == Components::Stance::StandGround
                    ? Reach(*rules) : SearchReach(*rules, building);
            const entt::entity found = FindTarget(registry, grid, self, look, *rules, candidates);
            if (found != entt::null) Engage(registry, self, found, false);
            break;
        }
        case CombatState::Chasing: {
            const Core::Fixed32 gap = Gap(registry, self, combat.target);
            if (InReach(*rules, gap, Core::Fixed32(0))) {
                combat.state = CombatState::Attacking;
                registry.remove<Components::Path>(self);
                break;
            }
            // A predator gives up what has got further from it than it sees.
            if (!combat.ordered && gap > rules->los && Search::Hunts(registry, self)) {
                Disengage(registry, grid, self, combat);
                combat.scan_ms = HuntWait(registry, kHuntAgainMs);
                break;
            }
            const bool immobile = building || rules->speed.value <= 0;
            const bool holds = combat.stance == Components::Stance::StandGround;
            if (!combat.ordered && (immobile || holds)) {
                Disengage(registry, grid, self, combat);
                break;
            }
            if (immobile) break;
            if (!combat.ordered && combat.stance == Components::Stance::Defensive &&
                combat.has_post) {
                const auto& at = registry.get<Components::Transform>(self).position;
                const Core::Fixed32 strayed = Core::FixedMath::Distance(
                    at.x - combat.post.x, at.y - combat.post.y);
                if (strayed > rules->search_radius * Core::Fixed32(kDefensiveLeash)) {
                    Disengage(registry, grid, self, combat);
                    break;
                }
            }
            combat.chase_ms -= dt_ms;
            const auto* path = registry.try_get<Components::Path>(self);
            const bool walking = path != nullptr &&
                                 path->current_waypoint_index < path->waypoints.size();
            if (combat.chase_ms <= 0 || !walking) {
                if (walking && StillThere(registry, self, combat.target, *path)) {
                    combat.chase_ms = kChaseReplanMs;
                    break;
                }
                // Over the pass's plans, it waits for the next pass.
                if (plans >= kChasePlansPerPass) break;
                ++plans;
                combat.chase_ms = kChaseReplanMs;
                if (PlanChase(registry, grid, self, combat.target, *rules)) {
                    combat.chase_failures = 0;
                } else if (++combat.chase_failures >= kChaseGiveUp) {
                    // Nowhere to stand within reach of it: given up, ordered
                    // or not.
                    combat.chase_failures = 0;
                    Disengage(registry, grid, self, combat);
                }
            }
            break;
        }
        case CombatState::Attacking: {
            // It swings or shoots only once it faces its target; a ship or a
            // trebuchet turns to it an angle at a time.
            const auto* aim = registry.try_get<Components::Transform>(combat.target);
            const bool facing = aim == nullptr || Turning::Toward(registry, self, aim->position, dt_ms);
            if (combat.strike_ms >= 0) {
                combat.strike_ms -= dt_ms;
                if (combat.strike_ms > 0) break;
                combat.strike_ms = -1;
                // A blow begun lands: a target that has stepped off since the
                // swing began takes it all the same, as an arrow loosed hits.
                if (Shoots(*rules)) {
                    volleys.emplace_back(self, combat.target);
                } else if (const R::UnitRules* defender = DefendingRules(registry, combat.target)) {
                    blows.push_back({combat.target,
                                     Blow(Damage(rules->attacks, rules->attack_count, *defender),
                                          LevelOf(registry, grid, self),
                                          LevelOf(registry, grid, combat.target), false),
                                     self});
                    MeleeBlast(registry, grid, self, combat.target, *rules, blows);
                    if (SpentInBlow(*rules)) spent.push_back(self);
                }
                break;
            }
            if (!InReach(*rules, Gap(registry, self, combat.target), kDisengageSlack)) {
                combat.state = CombatState::Chasing;
                combat.chase_ms = 0;
                break;
            }
            if (combat.reload_ms <= 0 && facing) StartSwing(registry, self, combat, *rules);
            break;
        }
        }
    }

    // Applied after the pass: a death destroys entities, and loosing a missile
    // creates them, neither of which a live view may see.
    for (const auto& volley : volleys) {
        Missiles::Fire(registry, grid, volley.first, volley.second);
    }
    for (const Strike& blow : blows) {
        Hurt(registry, grid, blow.target, blow.amount, blow.attacker);
    }
    for (entt::entity ship : spent) {
        if (Alive(registry, ship)) Mortality::Kill(registry, grid, ship);
    }

    // Villagers whose fight, or run, is over go back to the job they broke
    // off.
    std::vector<entt::entity> resuming;
    auto paused = registry.view<const Components::Interrupted, const Components::Worker>();
    for (auto e : paused) {
        const auto* combat = registry.try_get<Combat>(e);
        if (combat != nullptr && combat->state != CombatState::Idle) continue;
        if (const auto* path = registry.try_get<Components::Path>(e); path != nullptr && path->has_goal) continue;
        resuming.push_back(e);
    }
    for (entt::entity e : resuming) {
        const Components::Interrupted kept = registry.get<Components::Interrupted>(e);
        registry.remove<Components::Interrupted>(e);
        Orders::GoBackToWork(registry, grid, e, kept.resource, kept.site);
    }
}

} // namespace ECS::Combat
