#pragma once
#include <entt/entt.hpp>

#include "core/Command.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Carrying out the players' orders, inside the tick.
//
// Each command is checked against the player who gave it -- that player has
// to own what it orders about -- and then handed to the module whose business
// it is: production queues, the buildings module, the worker's job. Nothing
// here decides how a thing is done, only that the order is allowed and who
// carries it out.
namespace ECS::Orders {

// Applies one order. Orders that fail their checks change nothing.
void Execute(entt::registry& registry, Physics::Grid& grid, const Core::Command& command);

// What a unit does on its way out of a building with a rally point: walk to the
// point, or -- a villager rallied onto a resource -- go and gather it. The same
// thing the matching order does, without the order's ownership check, since the
// building that trained it already passed one.
void FollowRally(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
                 const Components::Vector2D& at, entt::entity target);

// A villager the town bell sent for cover, going back to what it was doing:
// the resource it was working, or the site it was building. Nothing, if both
// are gone.
void GoBackToWork(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
                  entt::entity resource, entt::entity site);

// A villager breaking off its job for a fight: the job is kept
// (Components::Interrupted) for GoBackToWork, and it stands, free to fight or
// to run. Any order after drops the kept job.
void Interrupt(entt::registry& registry, entt::entity unit);

// Once a tick, before anything moves: a unit with waypoints that has come to
// the end of its walk sets off for the next.
void WalkOnToWaypoints(entt::registry& registry, const Physics::Grid& grid);

} // namespace ECS::Orders
