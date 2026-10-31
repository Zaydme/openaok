#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <vector>

#include "ai/Language.h"
#include "core/GameSetup.h"
#include "core/Types.h"

// What a computer player's rules remember between passes: the state the
// facts read and the actions write. Plain data, one per computer seat, kept
// in its brain (ai/Brain).
//
// - which rules are still enabled (disable-self);
// - goals 1-40, set-goal / goal;
// - every strategic number, starting at its default;
// - timers 1-10, each off or due at a game time; "triggered" from then until
//   disabled;
// - the last random number generate-random-number drew;
// - escrow: a share of everything gathered set aside per resource, which the
//   plain can-* facts and actions may not spend and the -with-escrow ones may;
// - taunts detected from each player, trigger events and signals;
// - the player's own diplomatic stance toward each other player;
// - the difficulty parameters and doctrine the scripts set.
namespace AI {

struct Timer {
    bool on = false;
    int64_t due_ms = 0;
};

struct Mind {
    std::vector<uint8_t> enabled;                // by rule
    std::array<int, Lang::kGoals> goals{};
    std::array<int, Lang::kStrategicNumbers> sn{};
    std::array<Timer, Lang::kTimers> timers{};
    int random_number = 0;
    // Escrow, by Core::ResourceType: the percentage set aside, how much is,
    // and the stock at the end of the last pass -- what has come in since is
    // what was gathered.
    std::array<int, 4> escrow_percent{};
    std::array<int, 4> escrow{};
    std::array<int, 4> last_stock{};
    bool stock_seen = false;
    // Taunts detected, by the player who sent them.
    std::array<std::bitset<256>, Core::kMaxPlayers> taunts{};
    std::bitset<Lang::kEvents> events;
    std::bitset<Lang::kSignals> signals;
    // This player's stance toward each seat (Lang::Stance).
    std::array<uint8_t, Core::kMaxPlayers> stance{};
    std::array<int, 2> difficulty_parameters = {100, 100};
    int doctrine = 0;
    // When the player's current age began, for current-age-time.
    int age = 0;
    int64_t age_since_ms = 0;
    // Facts that stay true once true: resource-found, by Core::ResourceType.
    std::array<bool, 4> found{};
    // Tribute given to this player, by giver and Core::ResourceType: in all,
    // and since the rules last cleared their memory of it.
    std::array<std::array<int32_t, 4>, Core::kMaxPlayers> tribute{};
    std::array<std::array<int32_t, 4>, Core::kMaxPlayers> tribute_memory{};
};

// Sets a strategic number as the game takes one, from set-strategic-number
// or as its default:
// - below Hardest, unless sn-do-not-scale-for-difficulty-level is 1, a
//   positive attack group size, number of attack groups, enemy-sighted
//   response or percent of soldiers sent to attack is scaled down for the
//   difficulty (whole numbers, never below 1), and the initial attack delay is
//   lengthened (Hard scales only in a scenario game);
// - the gatherer percentages are kept to 0-100, the enemy-sighted response
//   distance to 50, the group form distance to 30, the boar hunt group to 8;
// - a positive explore group size is 1.
// Without a lobby (`setup` null) nothing is scaled.
void SetStrategic(Mind& mind, Lang::SN sn, int value, const Core::GameSetup* setup);

// Fresh state for a program of `rules` rules: every rule enabled, every
// strategic number set to its default.
void Reset(Mind& mind, size_t rules, const Core::GameSetup* setup = nullptr);

} // namespace AI
