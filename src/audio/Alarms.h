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

// An alarm that has sounded: what was struck, where, and by whom -- a
// player, or Gaia for a wild animal; `conversion` for a monk at work.
struct Raised {
    entt::entity subject = entt::null;
    ECS::Components::Vector2D at{};
    Core::PlayerId by = Core::PLAYER_GAIA;
    bool conversion = false;
};

// The alarms that have sounded and not yet been taken, oldest first. The
// minimap shows each, and the message lines name the attack
// (rendering/Alerts).
bool TakeRaised(Raised& alarm);

} // namespace Audio::Alarms
