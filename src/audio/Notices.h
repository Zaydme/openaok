#pragma once
#include <string>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/Events.h"

// The interface's sounds for what passes between players: a trade at the
// Market (buy.wav, sell.wav), a tribute received (tribute.wav), a chat
// message received (chatrcvd.wav), a flare (flare.wav), and the taunts -- a
// chat message that is a number from 1 to 30 plays the recorded line of
// that number, Taunt/01 Yes.mp3 ... 30 Wololo.mp3. And the notifications'
// own: research finished at one of the player's buildings (resdone.wav for an
// age, resdone2.wav for anything else; chatrcvd.wav for another player's
// age), a sheep found (capsheep.wav), a farm or fish trap run out
// (farmdied.wav), a relic picked up (puprelic.wav), production halted for
// want of houses (needhous.wav), one of Gaia's taken (capgaia.wav).
// Interface sounds by their interfac.drs file ids (audio/InterfaceSounds.h),
// heard only by the player they concern -- but for a Wonder, begun
// (mstart.wav), finished (mdone.wav) or destroyed (mkilled.wav), which every
// player hears.
namespace Audio::Notices {

// Where the taunts are: the Taunt folder beside Data.
void SetTauntFolder(const std::string& folder);

// Forgets what has been heard, for a new world.
void Reset();

// Everything up to `tick` already heard: a loaded game's old news.
void CatchUp(uint64_t tick);

// Takes one event from the simulation's log.
void Hear(const entt::registry& registry, const ECS::Events::Event& event,
          Core::PlayerId listener);

// Once a frame: the chat lines and flares sent the listener since the last.
void HearMessages(const entt::registry& registry, Core::PlayerId listener);

// The taunt file for a number from 1 to 30; empty when there is none.
std::string TauntFile(int taunt);

} // namespace Audio::Notices
