#pragma once
#include <entt/entt.hpp>

#include "physics/Grid.h"

// What a ranged attack looses, from loosing to landing.
//
// Every missile is a unit of the dat's own -- an arrow, a spear, a javelin --
// with its own speed, arc and graphic, and the shooter's numbers say which.
// Accuracy is rolled when it is loosed: a hit flies at where the target stands
// then, a miss at a point beside it. Either way it flies to the ground, not
// after its target, so a unit that walks on is missed and whoever stands where
// the missile comes down can be hit instead.
namespace ECS::Missiles {

// How many missiles a shooter looses at once: its own, and -- for a building --
// the arrows its garrison adds, which are its secondary missile. A unit looses
// one. A building's garrison adds one arrow for each share of the building's
// own pierce damage a second its occupants lend it, up to the dat's most.
struct Volley {
    int own = 0;
    int garrison = 0;
    int Total() const { return own + garrison; }
};
Volley VolleyOf(const entt::registry& registry, entt::entity shooter);

// Looses `shooter`'s volley at `target`.
void Fire(entt::registry& registry, const Physics::Grid& grid, entt::entity shooter,
          entt::entity target);

// Flies every missile one tick, and lands those that arrive.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Missiles
