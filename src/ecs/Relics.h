#pragma once
#include <entt/entt.hpp>

#include "physics/Grid.h"

// Relics: picked up by a monk, carried to a monastery, and earning gold there.
//
// Who may do what is the dat's: a unit whose tasks pick up the relic's unit
// (a monk's "pickup" task) can fetch one, and doing so it becomes the dat's
// monk carrying a relic -- the unit whose tasks deposit at a monastery. It
// puts the relic in one of its owner's monasteries and is a monk again. A
// carrier that dies drops the relic where it fell; a monastery that falls
// leaves its relics on the ground.
//
// Each relic held earns half a gold a second. The owner's resource 7 counts
// the relics it holds.
namespace ECS::Relics {

bool Fetch(entt::registry& registry, entt::entity unit, entt::entity relic);
bool Deposit(entt::registry& registry, entt::entity bearer, entt::entity monastery);

// Whether `unit` could fetch `relic`, or put the relic it carries in `monastery`.
bool CanFetch(const entt::registry& registry, entt::entity unit, entt::entity relic);
bool CanDeposit(const entt::registry& registry, entt::entity unit, entt::entity monastery);

// Leaves the relics `building` holds on the ground beside it -- it is being
// torn down.
void Release(entt::registry& registry, entt::entity building);

// How many relics a building holds.
int Held(const entt::registry& registry, entt::entity building);

// Once a tick: errands walk and are done, shrines pay, the fallen drop theirs.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Relics
