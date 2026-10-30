#pragma once
#include <cstdint>

#include <entt/entt.hpp>

// What each kind of unit looks for round itself: the dat classes its kind
// searches for. A unit picks a fight on sight only with something among them
// -- a wolf goes after villagers and soldiers, never a building, a ram or a
// Monk. The kind goes by the unit's class (the siege weapons also by their
// unit), and a soldier looks for wolves only when the computer plays it.
namespace ECS::Search {

// The classes `self` looks for: bit c for dat class c.
uint64_t Sought(const entt::registry& registry, entt::entity self);

// Whether `classes`, from Sought, holds dat class `cls`.
bool Holds(uint64_t classes, int cls);

} // namespace ECS::Search
