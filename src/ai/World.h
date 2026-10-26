#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/GameSetup.h"
#include "core/Types.h"

// The world as a computer player's rules see it, taken once per rule pass.
//
// The facts about other players come in three kinds, and this is where each
// gets its numbers:
//
//   - the timeline (players-population, -civilian-population,
//     -military-population, -current-age): exact, as the timeline shows it;
//   - what the player has seen (players-unit-type-count,
//     players-building-type-count ...): only what it has seen, except for
//     allies and itself, which are counted in full;
//   - the cheating facts (cc-players-*): everything, fog or not.
//
// Counts are kept by a unit's *form*: an upgrade keeps an entity's identity
// (the line's first unit) while the player's slot for it takes the upgrade's
// numbers and art, so a Man-at-Arms is an entity of unit 74 whose owner's
// slot 74 wears 75. The form is the line member the slot currently wears;
// a unit-type-count of man-at-arms counts forms of 75, of militiaman-line
// every member. Villagers count as the villager (83), whatever job unit
// they wear and whether born male or female.
namespace AI {

struct Seat {
    bool valid = false;      // a seat in the lobby
    bool in_game = false;    // not yet defeated
    bool resigned = false;
    bool computer = false;
    int civ = 0;
    Core::TeamId team = 0;
    int age = 0;
    int64_t age_since_ms = 0;
    int population = 0;
    int civilians = 0;
    int military = 0;
};

// A player's things by form: standing (finished buildings, units out in the
// world) and in all (with foundations, and units queued in its buildings).
struct Counts {
    std::vector<int16_t> units;
    std::vector<int16_t> units_total;
    std::vector<int16_t> buildings;
    std::vector<int16_t> buildings_total;
    int unit_count = 0;
    int unit_total = 0;
    int building_count = 0;
    int building_total = 0;
};

struct World {
    Seat seats[Core::kMaxPlayers];
    Counts all[Core::kMaxPlayers];    // fog ignored
    Counts seen[Core::kMaxPlayers];   // what `viewer` has seen; all, for itself and allies
    Core::PlayerId viewer = 0;
    // Each player's form of each unit slot: the line member it wears now.
    std::vector<int16_t> form_of[Core::kMaxPlayers];
};

// The unit a player's slot `unit_id` is now, among its line's members --
// `unit_id` itself when it is no line's or wears none of them.
int FormOf(const entt::registry& registry, Core::PlayerId player, int unit_id);

// Fills `world` for `viewer`. `allied[p]` says whether p counts as an ally,
// whose units the viewer counts without fog; `ages` and `age_since` are
// every seat's age and when it began, kept by the brains.
void Take(const entt::registry& registry, Core::PlayerId viewer, const bool* allied,
          const int64_t* age_since, World& world);

// How many of a thing -- a line, or one of its members -- a counts table holds.
// `ids`/`count` are the line; `member` -1 for the whole line.
int CountOf(const std::vector<int16_t>& table, const int* ids, int count, int member);

} // namespace AI
