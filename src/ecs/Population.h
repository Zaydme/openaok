#pragma once
#include <entt/entt.hpp>

#include "core/Rules.h"
#include "core/Types.h"

// How many units a player has, and how many it can support.
//
// Kept as two of the player's resources, updated by the units themselves:
// every unit's storage adds one to current population and takes one from
// headroom while it lives, and every Town Center and House adds five headroom
// while it stands (a Castle twenty). Current population plus headroom is what
// the player can support.
//
// A unit in training counts from the moment production starts, so two
// buildings cannot each start a unit into the same last free slot.
//
// The interface and production both read this module, so they always agree.
namespace ECS::Population {

// A unit's population cost: the unpaid headroom slot of its price.
int CostOf(const Core::Rules::UnitRules& unit);

// Units the player has, counting those in training.
int Used(const entt::registry& registry, Core::PlayerId player);

// Units the player can support, never more than the lobby's limit.
int Capacity(const entt::registry& registry, Core::PlayerId player);

// Whether one more unit costing `cost` population would fit.
bool HasRoom(const entt::registry& registry, Core::PlayerId player, int cost);

} // namespace ECS::Population
