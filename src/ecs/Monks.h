#pragma once
#include <entt/entt.hpp>

#include "physics/Grid.h"

// Monks: converting the enemy and healing their own side.
//
// Which units may do either is the dat's -- a unit with a Convert task
// converts, one with a Heal task heals -- and so are the figures it can give:
// the range, the shortest a conversion can take (the Convert task's work
// value: four seconds for a monk), how fast a heal goes (the Heal task's),
// and how quickly faith comes back (the civ's faith recharging rate). A monk
// needs full faith to convert and spends all of it in converting. Monks may
// convert monks, and buildings, only once their owner's resources say so
// (Atonement and Redemption set them).
//
// Not in the dat: faith's ceiling, 100, and how long a conversion may drag on
// -- each second past the shortest it takes with a 28% chance, and at ten
// seconds it takes for certain.
namespace ECS::Monks {

// Whether `monk` may convert `target` now: a convert task, enemies, the
// owner's resources, and full faith.
bool CanConvert(const entt::registry& registry, entt::entity monk, entt::entity target);

// Sends `monk` to convert `target`, or to heal one of its own side.
bool Convert(entt::registry& registry, entt::entity monk, entt::entity target);
bool Heal(entt::registry& registry, entt::entity monk, entt::entity target);

// Once a tick: faith comes back, conversions and heals go on, and an idle
// monk looks for its own wounded nearby.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Monks
