#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/GameSetup.h"
#include "core/Types.h"

// Each player's history of the game, for the Achievements screen's Timeline.
// Saved with the game.
//
// It is two lists. One reading of the player's population every 25 game
// seconds: the civilian and military population, r37 and r40, as two 16-bit
// numbers. The first reading is taken as the game starts, after every seat's
// units are placed.
//
// And events, newest first, each with how many readings had been taken -- so
// it is drawn at the next -- the game time, and up to three figures; none is
// kept before the first reading:
//
//   0, 1, 2  the Feudal, Castle and Imperial Age reached
//   4, 6     a Wonder, a Castle finished, the owner noted
//   5, 7     a Wonder, a Castle destroyed, kept by whoever brought it down
//            with its owner noted
//   11       a battle, below
//   20       out of the game other than by resigning
//
// A battle is the reading's own business. Each reading compares what the
// player has killed and razed since the last -- r170 and r172 -- with what it
// owned at the last, r164 and r165: 12 percent or more and it is fighting.
// While it fights, each reading adds what it destroyed and lost (r152 and
// r153 too) over what it owned to the battle's weight; the first reading that
// is not fighting ends it, and a battle of weight 0.15 or more is an event
// naming the player it destroyed most of since the battle began (r110.. and
// r126..). Only three battles stay on the list: a new one pushes out the
// weaker of the two before it, or goes itself.
//
// Game state, fixed point throughout: the weights are 32.32.
namespace ECS::Timeline {

// The event kinds, by number.
enum Kind : uint8_t {
    kFeudalAge = 0,
    kCastleAge = 1,
    kImperialAge = 2,
    kWonderBuilt = 4,
    kWonderDestroyed = 5,
    kCastleBuilt = 6,
    kCastleDestroyed = 7,
    kBattle = 11,
    kDefeated = 20,
};

// Seconds between readings.
constexpr int kSampleSeconds = 25;
// Battles kept on a player's list.
constexpr int kBattlesKept = 3;

struct Sample {
    int16_t civilian = 0;   // r37
    int16_t military = 0;   // r40
};

// One event.
struct Event {
    uint8_t kind = 0;
    int32_t sample = 0;          // the readings taken when it happened
    int32_t time_ms = 0;         // the world's clock
    int32_t a = 0;               // the other player (a Wonder's, a Castle's owner;
                                 //  a battle's enemy; the player that killed most of a
                                 //  defeated one's units)
    Core::Fixed32 b;             // a battle's weight; a defeat's kill count
    int32_t c = 0;               // 1 for a battle kept
};

// What the readings compare against: the counters at the last reading and,
// for a battle, those against each enemy when it began.
struct Last {
    int32_t razings = 0;            // r43
    int32_t razing_value = 0;       // r172
    int32_t buildings_lost = 0;     // r155
    int32_t building_value_lost = 0;// r153
    int32_t units_killed = 0;       // r20
    int32_t military_value = 0;     // r170
    int32_t units_lost = 0;         // r154
    int32_t value_lost = 0;         // r152
    int32_t unit_value_owned = 0;   // r164
    int32_t building_value_owned = 0;   // r165
    int32_t kill_value[8] = {};     // r110..r117
    int32_t razing_value_of[8] = {};// r126..r133
};

// One player's history.
struct Record {
    std::vector<Sample> samples;
    std::vector<Event> events;      // newest first
    int32_t seconds = 0;            // game seconds since the last reading
    bool fighting = false;          // in a battle
    Core::Fixed32 weight;           // the battle's weight so far
    Last last;
};

struct Table {
    Record records[Core::kMaxPlayers];
};

void Setup(entt::registry& registry);

// The first reading, once every seat's units are in the world.
void Start(entt::registry& registry);

// A game second has passed: every 25th, a reading for each player.
void Second(entt::registry& registry);

// An event for `player`, stamped with the game clock `time_ms`. Nothing
// before the first reading.
void Note(entt::registry& registry, Core::PlayerId player, Kind kind, int32_t time_ms,
          int32_t a = 0, Core::Fixed32 b = Core::Fixed32(0), int32_t c = 0);

// `player` is out of the game, not by resigning.
void Defeated(entt::registry& registry, Core::PlayerId player, int32_t time_ms);

const Record* Of(const entt::registry& registry, Core::PlayerId player);

} // namespace ECS::Timeline
