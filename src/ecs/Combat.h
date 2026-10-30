#pragma once
#include <entt/entt.hpp>

#include "core/Random.h"

#include "core/FixedPoint.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "physics/Grid.h"

// Fighting.
//
// Damage is the dat's: for every attack class the attacker has, the defender's
// armour of that class is taken off, and what is left of each is added up --
// never less than one. A class the defender has no armour entry for meets its
// base armour, which for nearly everything is so high the class does nothing:
// that is how a spearman's bonus against cavalry hurts a knight and not an
// archer. Reach is measured edge to edge, so a melee unit hits a building from
// its wall and an archer's range is its range to the target, not to its middle.
//
// Who fights whom is the dat's too. A unit's combat level is its target
// priority -- military before villagers before buildings -- and only
// military units and armed buildings look for fights; villagers and most
// animals only answer one. Its search radius is how far it looks.
//
// The other halves live beside it: ecs/Missiles flies what a ranged attack
// looses, and ecs/Mortality takes the dead off the field.
namespace ECS::Combat {

// The rules something fights with: its owner's slot for it -- or, for a
// villager at a job, that job's unit, which is how a hunter comes to throw a
// spear where a villager would punch.
const Core::Rules::UnitRules* FightingRules(const entt::registry& registry,
                                            entt::entity entity);

// The rules something is defended by: its owner's slot for its own identity.
const Core::Rules::UnitRules* DefendingRules(const entt::registry& registry,
                                             entt::entity entity);

// The damage one blow of `attacks` does to `defender`.
int Damage(const Core::Rules::ClassAmount* attacks, int attack_count,
           const Core::Rules::UnitRules& defender);

// A blow's damage, all told: the classes' damage less armour (`Damage`),
// times a quarter more struck from higher ground or a quarter less from lower,
// never less than one -- and then halved for a stray missile that hit someone
// it was not aimed at. Fractional, as hit points are.
Core::Fixed32 Blow(int damage, int attacker_level, int target_level, bool stray);

// The same, as a whole number, for checks and the interface.
int HeightAdjusted(int damage, int attacker_level, int target_level);

// The elevation level of the ground under an entity.
int LevelOf(const entt::registry& registry, const Physics::Grid& grid, entt::entity entity);

// Whether `entity` is still in the fight: present, with hit points left, and
// not already dying.
bool Alive(const entt::registry& registry, entt::entity entity);

// Whether `a` would fight `b` on sight: `a`'s owner regards `b`'s as an
// enemy (ecs/Diplomacy) -- a neutral's units are left alone unless ordered.
// Gaia's animals are enemies only if they hunt -- a wolf; a deer or a sheep
// is never attacked on sight, only on an order.
bool Hostile(const entt::registry& registry, entt::entity a, entt::entity b);

// Whether `b` is on `a`'s side: the same owner, or one `a`'s owner regards
// as an ally. Never struck by `a`'s blast, nor answered when it strikes.
bool Friendly(const entt::registry& registry, entt::entity a, entt::entity b);

// The ground something covers, as the original measures its distances: a
// building its footprint, anything else the box its dat size spans each way
// about its middle.
Buildings::Bounds BoxOf(const entt::registry& registry, entt::entity entity);

// How far apart two things are, edge to edge: the straight distance between
// their boxes (BoxOf), zero where they touch or overlap.
Core::Fixed32 Gap(const entt::registry& registry, entt::entity from, entt::entity to);

// The wild predators' own random numbers, for how long each waits between
// looks round: apart from the world's, so their looking does not shift every
// other roll in the world.
struct HuntRandom {
    Core::Random rng{1};
};

// A wild predator's first look round for prey, as long after it appears as
// it waits between looks.
void FirstLook(entt::registry& registry, entt::entity predator);

// Turns `self` to face `target`; a building keeps its art as it is.
void Face(entt::registry& registry, entt::entity self, entt::entity target);

// Sets `attacker` on `target`. An ordered target is held until another order;
// one picked up on its own can be dropped.
void Engage(entt::registry& registry, entt::entity attacker, entt::entity target,
            bool ordered);

// Takes `amount` hit points from `target`, which may answer `attacker` and
// may die of it. A death is credited to `attacker_player` -- a missile's
// owner, whose shooter may be gone by the time it lands -- or, when that is
// -1, to the attacker's owner (ecs/Score).
void Hurt(entt::registry& registry, Physics::Grid& grid, entt::entity target,
          Core::Fixed32 amount, entt::entity attacker, int attacker_player = -1);

// Everything that fights, for one tick: finds targets, closes on them, and
// strikes or shoots on its reload.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Combat
