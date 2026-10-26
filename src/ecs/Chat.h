#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"

// Chat between players, and the taunts typed into it.
//
// A message is an order like any other -- it travels in lockstep, so every
// machine has the same conversation -- but nothing in the simulation reads
// it. Carried out inside the tick, it is written to this log, which, like the
// event log (ecs/Events), the simulation only writes: the interface prints
// what its player is sent, and the sound plays the taunts. It is not in the
// world's digest.
//
// Who hears it is decided as it is carried out, from the sender's stances:
// everyone, the players the sender regards as allies, or one player -- the
// sender always. "You cannot chat after you have resigned or been defeated"
// (string 3070). With Lock Teams there is no "chatting privately with
// nonallied players" (string 30276).
//
// A message that is a number from 1 to 30 and nothing else is a taunt: the
// recorded line of that number (Taunt/01 Yes.mp3 ... 30 Wololo.mp3) plays for
// whoever is sent it, and the number shows as the text, as typed.
namespace ECS::Chat {

enum class Audience : uint8_t { Everyone = 0, Allies = 1, Player = 2 };

// The most characters a message holds; longer ones are cut.
constexpr size_t kMostCharacters = 65;
constexpr int kTaunts = 30;
// The lines kept; older ones are let go.
constexpr size_t kKept = 128;

struct Line {
    uint64_t tick = 0;
    Core::PlayerId from = 0;
    // A bit per player who hears it, the sender among them.
    uint16_t to = 0;
    Audience audience = Audience::Everyone;
    std::string text;
    // The taunt it is, 1 to 30, or 0.
    int taunt = 0;

    bool Heard(Core::PlayerId player) const { return (to >> player) & 1u; }
};

struct Log {
    std::vector<Line> lines;
};

void Setup(entt::registry& registry);

// The taunt a message is: 1 to 30 when it is that number alone (spaces
// round it aside), else 0.
int TauntOf(const std::string& text);

// Carries out a message from `from`: to everyone, to its allies, or to
// `player`. False, logging nothing, from a player who is out of the game, an
// empty message, one to nobody, or -- with teams locked -- one in private to
// a player it does not regard as an ally.
bool Post(entt::registry& registry, Core::PlayerId from, Audience audience,
          Core::PlayerId player, const std::string& text);

// The log; null if the world was built without one.
const Log* Read(const entt::registry& registry);

} // namespace ECS::Chat
