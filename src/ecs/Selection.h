#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"

// What the player has selected, asked two different ways.
//
// "What is the player looking at?" covers anything that can be clicked -- a
// tree, a gold mine, a sheep, someone else's villager -- because the interface
// describes whatever is picked. "What can the player give orders to?" covers
// only its own units and buildings, because only those get a command grid and
// a delete button.
namespace ECS::Selection {

// The entity the interface describes. When the player's own entities are
// among the selection, one of those is preferred: the panel should show what
// the player is commanding, not whatever else the box happened to catch.
entt::entity Inspected(const entt::registry& registry, Core::PlayerId player);

// The entity the player can give orders to, or null when nothing it owns is
// selected.
entt::entity Commanded(const entt::registry& registry, Core::PlayerId player);

} // namespace ECS::Selection
