#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Nearby.h"

#include <algorithm>
#include <vector>

#include "core/FixedMath.h"
#include "ecs/Animals.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Missiles.h"
#include "ecs/Navigation.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/VillagerJobs.h"
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

// How far past touching a melee blow reaches -- enough to strike a neighbour
// standing at the ordinary spacing units keep, 0.7 between middles -- and the
// slack before a unit in a fight decides its target has slipped away: enough
// that a target drifting on the spot does not make its attacker start and stop.
const Core::Fixed32 kMeleeReach = Core::Fixed32(35) / Core::Fixed32(100);
const Core::Fixed32 kDisengageSlack = Core::Fixed32(3) / Core::Fixed32(10);

// How often a chase re-plans its route, and how often an idle unit looks for
// a fight. Staggered per entity so a crowd does not all scan in one tick.
constexpr int32_t kChaseReplanMs = 500;
// Empty routes to a target before the chase is given up.
constexpr int kChaseGiveUp = 3;
constexpr int32_t kScanMs = 500;

// A defensive unit pursues no further from where it was than this many times
// its search radius before turning back.
constexpr int kDefensiveLeash = 2;

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

// Whether the target is within reach of these rules, by gap.
bool InReach(const R::UnitRules& rules, Core::Fixed32 gap, Core::Fixed32 slack) {
    if (Shoots(rules) && rules.max_range.value > 0) {
        return gap <= rules.max_range + slack && gap >= rules.min_range;
    }
    return gap <= rules.max_range + kMeleeReach + slack;
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

// The best target in reach for `self`: the highest combat level first, then
// the nearest, then the lowest entity id, so the choice is the same on every
// machine.
// `candidates` holds everything with a place, health and an owner, by where it
// stands.
entt::entity FindTarget(const entt::registry& registry, const Physics::Grid& grid,
                        entt::entity self, Core::Fixed32 reach, const R::UnitRules& own,
                        const Nearby::Buckets& candidates) {
    entt::entity best = entt::null;
    int best_level = -1;
    Core::Fixed32 best_gap{};
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
        // A foundation not yet begun is nobody's target.
        if (!Buildings::Begun(registry, other)) return;
        const Core::Fixed32 gap = Gap(registry, self, other);
        if (gap > reach) return;
        if (!CanReach(registry, grid, self, other, own)) return;
        const R::UnitRules* rules = DefendingRules(registry, other);
        const int level = rules != nullptr ? rules->combat_level : 0;
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
bool PlanChase(entt::registry& registry, const Physics::Grid& grid, entt::entity self,
               entt::entity target, const R::UnitRules& rules) {
    const auto& from = registry.get<Components::Transform>(self).position;
    const auto& at = registry.get<Components::Transform>(target).position;
    const Physics::Layer layer = Navigation::LayerOf(registry, grid, self);
    Components::Vector2D goal = at;
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
        const Core::Fixed32 reach = Shoots(rules) ? rules.max_range : kMeleeReach;
        ask.standoff = Radius(registry, self) + reach / Core::Fixed32(2);
        ask.clearance = Core::Fixed32(1) / Core::Fixed32(2);
        ask.layer = layer;
        const auto spot = Pathfinding::FindStandingSpot(grid, ask);
        if (spot.found) goal = spot.spot;
    }
    auto path = Navigation::Route(registry, grid, self, from, goal);
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

void Face(entt::registry& registry, entt::entity self, entt::entity target) {
    auto* sprite = registry.try_get<Components::Sprite>(self);
    if (sprite == nullptr || registry.all_of<Components::Building>(self)) return;
    const auto* from = registry.try_get<Components::Transform>(self);
    const auto* to = registry.try_get<Components::Transform>(target);
    if (from == nullptr || to == nullptr) return;
    const Core::Fixed32 dx = to->position.x - from->position.x;
    const Core::Fixed32 dy = to->position.y - from->position.y;
    if (dx.value != 0 || dy.value != 0) {
        sprite->facing = Core::FixedMath::WorldVectorToFacing(dx, dy);
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

void Engage(entt::registry& registry, entt::entity attacker, entt::entity target,
            bool ordered) {
    auto* combat = registry.try_get<Combat>(attacker);
    if (combat == nullptr || attacker == target || !registry.valid(target)) return;
    if (!combat->has_post || combat->state == CombatState::Idle) {
        combat->post = registry.get<Components::Transform>(attacker).position;
        combat->has_post = true;
    }
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

    // Answering an attack: anything armed and not told otherwise turns on
    // whoever hurt it -- a boar on its hunter, a villager on a wolf, a
    // soldier on a neutral's. One busy at a job keeps at it. Never on its own
    // side, nor on one its owner regards as an ally.
    if (health.current_hp > 0 && Alive(registry, attacker) &&
        !Friendly(registry, target, attacker)) {
        if (auto* combat = registry.try_get<Combat>(target)) {
            const auto* worker = registry.try_get<Components::Worker>(target);
            // Running for cover is busy too: it keeps running.
            const bool busy = (worker != nullptr && worker->state != Core::UnitState::Idle) ||
                              registry.all_of<Components::Boarding>(target);
            if (combat->state == CombatState::Idle && !busy &&
                combat->stance != Components::Stance::NoAttack &&
                !IsBuilding(registry, target)) {
                Engage(registry, target, attacker, false);
            }
        }
    }
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
    std::vector<Strike> blows;
    std::vector<std::pair<entt::entity, entt::entity>> volleys;   // shooter, target
    std::vector<entt::entity> spent;   // demolition ships that have detonated

    // What can be fought, by where it stands; nothing moves during the pass.
    static Nearby::Buckets candidates;
    Nearby::Build(candidates,
                  registry.view<const Components::Transform, const Components::Health, const Components::Owner>(),
                  grid.GetWidth(), grid.GetHeight());

    auto fighters = registry.view<Combat, Components::Transform>();
    for (auto self : fighters) {
        auto& combat = fighters.get<Combat>(self);
        if (!Alive(registry, self)) continue;
        combat.reload_ms = std::max<int32_t>(0, combat.reload_ms - dt_ms);
        combat.swing_ms = std::max<int32_t>(0, combat.swing_ms - dt_ms);

        const R::UnitRules* rules = FightingRules(registry, self);
        if (rules == nullptr || rules->attack_count == 0) continue;
        const bool building = IsBuilding(registry, self);

        if (combat.state != CombatState::Idle && !Alive(registry, combat.target)) {
            Disengage(registry, grid, self, combat);
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
            combat.scan_ms = kScanMs + static_cast<int32_t>(entt::to_integral(self) % 8) * 16;
            // Stand ground looks only as far as it can reach.
            const Core::Fixed32 look =
                combat.stance == Components::Stance::StandGround
                    ? rules->max_range + kMeleeReach : SearchReach(*rules, building);
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
            Face(registry, self, combat.target);
            if (combat.strike_ms >= 0) {
                combat.strike_ms -= dt_ms;
                if (combat.strike_ms > 0) break;
                combat.strike_ms = -1;
                if (Shoots(*rules)) {
                    volleys.emplace_back(self, combat.target);
                } else if (InReach(*rules, Gap(registry, self, combat.target),
                                   kDisengageSlack)) {
                    if (const R::UnitRules* defender =
                            DefendingRules(registry, combat.target)) {
                        blows.push_back({combat.target,
                                         Blow(Damage(rules->attacks, rules->attack_count,
                                                     *defender),
                                              LevelOf(registry, grid, self),
                                              LevelOf(registry, grid, combat.target),
                                              false),
                                         self});
                        MeleeBlast(registry, grid, self, combat.target, *rules, blows);
                        if (SpentInBlow(*rules)) spent.push_back(self);
                    }
                }
                break;
            }
            if (!InReach(*rules, Gap(registry, self, combat.target), kDisengageSlack)) {
                combat.state = CombatState::Chasing;
                combat.chase_ms = 0;
                break;
            }
            if (combat.reload_ms <= 0) StartSwing(registry, self, combat, *rules);
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
}

} // namespace ECS::Combat
