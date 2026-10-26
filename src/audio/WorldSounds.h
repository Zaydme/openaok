#pragma once
#include <entt/entt.hpp>

#include "audio/Listener.h"
#include "core/Types.h"

// The sounds the world's own animations make.
//
// The dat ties these to graphics, not to events: a dying graphic carries its
// scream, an attack graphic rings a sword on the frame the blow lands, a
// lumberjack's work graphic chops on the frame the axe bites. This watches
// what is being shown -- each visible unit's graphic and frame -- and plays
// what the dat says belongs to a graphic as it starts and to each frame as it
// is reached.
namespace Audio::WorldSounds {

// One frame's worth: compares every animated entity in earshot with how it
// was last seen. An entity first coming into earshot plays nothing until its
// animation next moves on, so scrolling over a battlefield does not replay
// every scream on it.
void Update(const entt::registry& registry, const Listener& listener,
            Core::PlayerId viewer);

// Forgets every entity seen, for a new world.
void Reset();

} // namespace Audio::WorldSounds
