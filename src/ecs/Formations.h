#pragma once
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/Command.h"
#include "ecs/components/Transform.h"

// Where each member of a group stands when it marches in formation.
//
// A group is laid out about where it was sent, facing the way it marches:
// hand-to-hand fighters at the front, those that shoot behind them.
// Line is ranks a little wider than they are deep; staggered is the same with
// gaps, against arrows; box puts the fighters round the outside and the rest
// within; flank splits the group in two either side of the line of march.
//
// Builders and gatherers are never laid out like this; each finds its own
// spot.
namespace ECS::Formations {

// Each unit's place, for a group sent to (x, y). Units the layout cannot place
// -- a building, something that does not move -- are left out.
std::vector<std::pair<entt::entity, Components::Vector2D>> Layout(
    const entt::registry& registry, const std::vector<entt::entity>& units,
    Components::Vector2D destination, Core::Formation formation);

} // namespace ECS::Formations
