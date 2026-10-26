#pragma once
#include "core/Types.h"
#include "ecs/Players.h"
#include "ecs/components/Worker.h"

// Which buildings take which resource.
//
// Every villager job unit in the dat lists the buildings it may hand a load
// to -- a lumberjack the Town Centre and the Lumber Camp, a forager the Town
// Centre and the Mill.
//
// Answers both "may I unload here?" (the worker system) and "is this
// building any use to me at all?" (choosing where to walk).
namespace ECS::DropSites {

// Whether a building of `building_unit_id` accepts `resource` -- from a
// villager, or from `gatherer` when it is a unit that gathers as itself (a
// fishing ship, whose list names the Dock).
bool Accepts(int building_unit_id, Core::ResourceType resource, int gatherer = -1);

// Whether it accepts anything this worker is currently carrying.
bool AcceptsAnything(int building_unit_id, const Components::Worker& worker);

// Moves everything `building_unit_id` accepts out of `worker` and into
// `owner`'s stores. Whatever it does not take stays in hand. Returns how much
// was handed over, across all resources.
int Deposit(int building_unit_id, Components::Worker& worker, Players::Player* owner);

} // namespace ECS::DropSites
