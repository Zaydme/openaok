#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/Events.h"

// What the player's own units say: answering a selection, acknowledging an
// order, announcing they are trained -- and a building's sound as it is laid
// out. All from the dat's per-unit sound slots, in the owner's civ's voice.
//
// A villager answers an order as the worker it is about to become: told to
// cut a tree it is a lumberjack, and the lumberjack's attack sound is the
// dat's "chop" acknowledgement. One voice line at a time; a new one waits for
// none, but nor does it talk over itself.
namespace Audio::Voices {

enum class Order { Move, Attack, Work };

// The selection the player just made: the first selected unit of theirs (or
// one of Gaia's, a sheep) answers.
void Selected(const entt::registry& registry, Core::PlayerId listener);

// An order the player just gave `unit`. `target` is what it was sent at, if
// anything; for Work, the resource or site.
void Acknowledge(const entt::registry& registry, entt::entity unit, Order order,
                 entt::entity target, Core::PlayerId listener);

// A building laid out by the player.
void Placed(const entt::registry& registry, int unit_id, Core::PlayerId listener);

// Takes one event from the simulation's log: a unit of the listener's trained.
void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId listener);

} // namespace Audio::Voices
