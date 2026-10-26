#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/Events.h"

// The alarm that tells a player it is under attack.
//
// One alarm per battle, a battle being a 20x20-tile area round where the
// player was first struck. Further blows inside it sound nothing until the
// fighting there has stopped for ten seconds. A horn for military units, a
// bell for villagers and buildings, a horn and a wolf for a wild animal --
// never for a boar, which only answers a hunter.
//
// The alarms are interface sounds played by interfac.drs file id, not dat
// sounds.
namespace Audio::Alarms {

// Takes one event from the simulation's log.
void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId listener);

// Forgets every battle, for a new world.
void Reset();

// The alarms that have sounded and not yet been taken, oldest first: what
// was struck and where. The minimap shows each (rendering/Alerts).
bool TakeRaised(entt::entity& subject, ECS::Components::Vector2D& at);

} // namespace Audio::Alarms
