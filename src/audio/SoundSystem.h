#pragma once
#include <string>

#include <entt/entt.hpp>

#include "audio/Listener.h"
#include "core/Types.h"
#include "hal/SlpFile.h"

// The game's sound, as one presentation layer beside the renderer.
//
// It reads the simulation and never writes to it: once a frame it hears the
// world's animations (audio/WorldSounds) and the news in the simulation's
// event log (alarms, trained units). The interface calls audio/Voices directly
// for what the player does.
namespace Audio {

// Opens the device and the archives the sounds live in: sounds.drs, read here,
// and the interface archive already held open for its art, borrowed (null if
// it is not open). False when there is no sound to be had; the game plays on
// silently.
bool Start(const std::string& sounds_drs, const HAL::DrsArchive* interface_archive);
void Stop();

// One frame: everything heard since the last.
void Update(const entt::registry& registry, const Listener& listener, Core::PlayerId viewer);

// A loaded game: what happened before it was saved is not heard again.
void CatchUp(const entt::registry& registry);

} // namespace Audio
