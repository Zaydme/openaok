#pragma once
#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Types.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Units sheltering in buildings, and aboard transport ships.
//
// What may go where is the dat's, from both sides: a unit's task list says it
// can garrison into buildings (a Garrison task naming the building class), and
// the building's garrison type and capacity say whom it takes and how many --
// a Town Center fifteen villagers, foot soldiers and monks, but no cavalry.
//
// Inside, a unit is out of the world: it gives up its Transform, which is what
// every view of the world is keyed on, so nothing draws it, sees with it or
// targets it. It heals at the building's own rate and adds to the building's
// arrows. Turned out -- or when the building falls -- it steps out at the door.
//
// The town bell: every villager within 25 tiles of the Town Center runs to the
// nearest building that takes villagers and has room, within 25 tiles of both
// the bell and itself, nearest villagers first.
// Rung again, they come out and go back to what they were doing.
namespace ECS::Garrisons {

// Whether `building` would take `unit` now: finished, the same owner, a unit
// whose tasks include garrisoning there and whose kind it accepts, and room.
bool Accepts(const entt::registry& registry, entt::entity building, entt::entity unit);

// How many units are inside, and how many it holds at most.
int Count(const entt::registry& registry, entt::entity building);
int Capacity(const entt::registry& registry, entt::entity building);

// Sends `unit` to garrison in `building`; it walks there and goes in.
// False if the building would not take it.
bool Send(entt::registry& registry, entt::entity unit, entt::entity building);

// Puts `unit` straight inside `holder`, as a scenario places a unit already
// garrisoned (ecs/ScenarioStart.cpp): no walk, and none of Accepts' checks --
// the file says it is there.
void PutInside(entt::registry& registry, entt::entity unit, entt::entity holder);

// Turns everyone out of `building`, at its door -- or, a transport, lands them
// at the shore nearest it (Unload).
void Eject(entt::registry& registry, const Physics::Grid& grid, entt::entity building);

// Whether `holder` is a transport: a unit, not a building, with room aboard.
bool IsTransport(const entt::registry& registry, entt::entity holder);

// Sends a loaded transport to the shore nearest `at`, where everyone aboard
// steps onto the land beside it -- on any shore, not only its own player's.
void Unload(entt::registry& registry, const Physics::Grid& grid, entt::entity transport,
            Components::Vector2D at);

// Whether `building` has a town bell: the building its owner's villagers are
// trained at, which is how the dat identifies a Town Center.
bool HasBell(const entt::registry& registry, entt::entity building);

// Rings `town_center`'s bell, or -- if it is ringing -- calls everyone back.
void RingBell(entt::registry& registry, const Physics::Grid& grid, entt::entity town_center);

// Damage a second, as a garrison's arrows are reckoned: a villager's is 2.5;
// one that shoots, its secondary missile's pierce -- or, with none, its own
// pierce and hand-to-hand damage -- over its reload. Nothing for one that
// fights hand to hand.
Core::Fixed32 Firepower(const entt::registry& registry, entt::entity entity);

// What the units inside add to `building`'s missile count: each archer,
// villager, cavalry archer and hand cannoneer its firepower over the
// building's. Anyone else inside adds nothing.
Core::Fixed32 LentMissiles(const entt::registry& registry, entt::entity building);

// Once a tick: units on their way in walk and go in, units inside heal.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Garrisons
