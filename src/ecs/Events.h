#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "ecs/components/Transform.h"

// What happened in the world, for whoever presents it.
//
// The simulation decides; the interface, the sound and an AI only watch,
// learning here what changed: a unit was trained, something of a player's was
// struck, a building went up, a technology was finished. The log is not part
// of the state hash. The one reader inside the tick is the computer players
// (who struck their town); every machine writes the same log, so that stays
// deterministic, and a saved game carries the log.
//
// Events are stamped with the tick they happened on. A reader keeps the last
// tick it has seen and takes everything after it; the log holds a few seconds,
// so a reader that falls behind loses only stale news.
namespace ECS::Events {

enum class Kind : uint8_t {
    Born,        // a unit stepped out of a building: subject, its owner
    Struck,      // subject took a blow from `other`, an enemy
    Built,       // a building was finished: subject
    // `id` is the technology, `player` the researcher, subject the building
    // that researched it (null for one that came of itself).
    Researched,
    Died,        // subject began to die
    Converting,  // a monk (`other`) has begun converting subject, `player`'s
    Converted,   // subject now belongs to `player`; `id` is who had it
    Defeated,    // `player` is out of the game
    Won,         // `player` has won it
    // `player` traded at its Market: `id` is the dat resource, `amount` the
    // lots, bought when positive and sold when negative.
    Traded,
    // `player` was given a tribute of `amount` of dat resource `id` by
    // `from`.
    Tribute,
    // `player` set its stance toward `from` to `id` (Diplomacy::Stance), or
    // -- `from` equal to `player` -- allied victory on (`id` 1) or off.
    Stance,
    // A trade unit (subject) of `player`'s handed in `amount` gold, in
    // hundredths, at its own Market or Dock.
    TradeDelivered,
    GateLocked,  // subject, a gate of `player`'s, was locked
    GateUnlocked,
    Repaired,    // subject, `player`'s, is mended to full
    Packed,      // subject, a siege engine, finished packing (`id`: what it became)
    Unpacked,
    Cheat,       // `player` used the cheat `id` (ecs/Cheats)
    // A herdable (subject) has joined `player`, whose unit came near it; `id`
    // is who had it.
    Claimed,
    // A farm or fish trap (subject) of `player`'s has run out of food; `id`
    // is its unit.
    Exhausted,
    // A unit of `player`'s (subject) has picked up a relic (`other`).
    PickedUp,
    // The first work was done on a building site (subject) of `player`'s;
    // `id` is its unit.
    Begun,
    // `player`'s production has stopped for want of room: subject, the
    // first of its buildings to wait while none other was, cannot start the
    // unit `id`. `amount` is 1 when the player is at the game's population
    // limit, which more houses do not lift.
    Halted,
    // The first work on a foundation (subject) of `player`'s found its ground
    // taken: the builder (other, at `at`) is turned away and the foundation
    // left as it was. `id` is its unit.
    Obstructed,
    // `player` did Treason: the Kings of the players it is not allied with
    // are shown to its side.
    Treason,
};

struct Event {
    Kind kind = Kind::Born;
    uint64_t tick = 0;
    entt::entity subject = entt::null;
    entt::entity other = entt::null;
    Core::PlayerId player = 0;
    int id = -1;                       // a unit id or a technology id
    Components::Vector2D at{};
    int amount = 0;                    // how much: lots traded, a tribute's size
    Core::PlayerId from = 0;           // who gave it, for a tribute or a stance
};

struct Log {
    uint64_t tick = 0;
    std::vector<Event> events;
};

void Setup(entt::registry& registry);

// Starts a tick: advances the clock and forgets what is now stale.
void BeginTick(entt::registry& registry);

void Emit(entt::registry& registry, Event event);

// The log; empty if the world was built without one.
const Log* Read(const entt::registry& registry);

} // namespace ECS::Events
