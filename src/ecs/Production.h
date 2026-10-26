#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "physics/Grid.h"

// Buildings making units and researching technologies.
//
// Everything comes from the owner's own copy of the rules: which building
// trains a unit is the unit's train location and whether the player has it
// enabled; which researches a technology is the technology's location and
// whether its requirements are met; the price and the time are the rules'
// figures, which effects may have changed; whether there is room is the
// population module's answer. Nothing here names a unit, a building or a
// technology.
//
// Resources are paid when an item is queued, not when it starts, so a queue
// only ever holds what the player could afford at the time. Population is
// checked when a unit *starts* training and booked from then on; a unit that
// cannot start waits at the front of the queue -- "you need more houses" --
// and starts the moment there is room. A technology, once queued, is marked as
// being researched so it cannot be queued twice. Cancelling refunds the full
// price.
namespace ECS::Production {

// Why an order to queue was turned down. `None` means it was queued.
enum class Refusal : uint8_t {
    None,
    NoSuchBuilding,
    NotOwned,
    NotFinished,
    NotProducedHere,
    Unavailable,
    QueueFull,
    CannotAfford,
};

// Queues `unit_id` at `building` for `player`, paying for it now.
Refusal Enqueue(entt::registry& registry, entt::entity building, int unit_id,
                Core::PlayerId player);

// Queues research of `tech_id` at `building` for `player`, paying for it now.
Refusal EnqueueTech(entt::registry& registry, entt::entity building, int tech_id,
                    Core::PlayerId player);

// Removes the item in `slot` from `building`'s queue and refunds it. A unit
// already in training gives its population slot back too; a technology
// becomes available again. Returns whether anything was cancelled.
bool Cancel(entt::registry& registry, entt::entity building, int slot,
            Core::PlayerId player);

// Cancels everything in `building`'s queue, as when it is destroyed.
void Abandon(entt::registry& registry, entt::entity building);

// Advances every building's queue by `dt_ms`: starts the front item when it
// can, brings a finished unit into the world beside the building, and
// completes a finished technology.
void Update(entt::registry& registry, const Physics::Grid& grid, int32_t dt_ms);

// How long the front item of a queue takes in total, in milliseconds, for the
// building's owner. Zero for an empty queue. The interface draws progress
// against this.
int32_t FrontDuration(const entt::registry& registry, entt::entity building);

} // namespace ECS::Production
