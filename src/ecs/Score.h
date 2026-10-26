#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/GameSetup.h"
#include "core/Types.h"
#include "ecs/Players.h"

// The score, and the statistics the Achievements screen shows.
//
// A player's score is a set of elements, each one a resource of the player's
// own table read through a rule; the score is their sum, re-read every tick.
// Only seven elements score at all, each trunc(resource) / divisor *
// multiplier, never below zero:
//
//   military   = r170 / 5
//   economy    = food/10 + wood/10 + stone/10 + gold/10
//              + r98 / 5
//              + max(0, (r175 - r171) / 10)
//   technology = r99 / 5 + 10 * r22
//   society    = r184 / 5
//   total      = military + economy + technology + society
//
// every division an integer one on the truncated resource. The other
// counters are the Achievements' statistics. Each is written where the event
// happens -- see the hooks below -- and lives in the player's resource table,
// so a save carries them with it.
//
//   r170  value of every unit killed, building razed and unit converted
//   r98   value of the player's units and finished buildings (not Castles or
//         Wonders) while they stand, less what is converted away
//   r175  tribute sent; r171 tribute received
//   r99   price of every technology researched at a building
//   r22   percent of the map explored, counted once a game second
//   r184  value of every Castle and Wonder finished
//
// "Value" is the sum of the paid slots of the unit's price in its owner's
// table at that moment.
namespace ECS::Score {

// One player's score and statistics after the last update. Whole numbers
// throughout, as the screens print them.
struct Sheet {
    // The Score page and the F4 overlay: the four categories and the total.
    int32_t military = 0;
    int32_t economy = 0;
    int32_t technology = 0;
    int32_t society = 0;
    int32_t total = 0;

    // Military Stats.
    int32_t units_killed = 0;      // r20
    int32_t units_lost = 0;        // r154
    int32_t buildings_razed = 0;   // r43
    int32_t buildings_lost = 0;    // r155
    int32_t units_converted = 0;   // r41: conversions this player's monks made
    int32_t largest_army = 0;      // the most r40 (military population) ever held

    // Economy Stats.
    int32_t food_collected = 0;    // r166
    int32_t wood_collected = 0;    // r167
    int32_t stone_collected = 0;   // r168
    int32_t gold_collected = 0;    // r169: gathered, traded and relic gold
    int32_t trade_profit = 0;      // r101
    int32_t tribute_sent = 0;      // r53
    int32_t tribute_received = 0;  // r171

    // Technology Stats: when each age was reached, in game time, -1 if not.
    int32_t feudal_ms = -1;
    int32_t castle_ms = -1;
    int32_t imperial_ms = -1;
    int32_t map_explored = 0;      // r22, percent
    int32_t research_count = 0;    // r21

    // Society Stats.
    int32_t wonders = 0;           // r174: Wonders ever finished
    int32_t castles = 0;           // r173: Castles ever finished
    int32_t relics_captured = 0;   // r7
    int32_t relic_gold = 0;        // r100
    int32_t villager_high = 0;     // the most r37 (civilian population) ever held
    int32_t survived = 1;          // r45: 1 while still in the game, 0 once out
};

// The module's state in the registry's context, written out by the save
// module. The counters themselves are in the players' resource tables.
struct Table {
    Sheet sheets[Core::kMaxPlayers];
    // Game time: the time an age is recorded at.
    int32_t clock_ms = 0;
    // Game time toward the next count of the map explored, made once a game
    // second.
    int32_t explore_ms = 0;
};

// The dat resources the score reads and writes, by number.
namespace Res {
constexpr int RelicsCaptured = 7;
constexpr int UnitsKilled = 20;
constexpr int TechnologyCount = 21;
constexpr int MapExplored = 22;
constexpr int CivilianPopulation = 37;
constexpr int MilitaryPopulation = 40;
constexpr int Conversions = 41;
constexpr int Razings = 43;
constexpr int SurvivalToFinish = 45;
constexpr int TributeSent = 53;
constexpr int KilledFirst = 69;          // units of player 1 killed; 70 player 2 ...
constexpr int ValueOwned = 98;           // the economy's "building cost sum"
constexpr int ResearchValue = 99;        // "tech cost sum"
constexpr int RelicIncome = 100;
constexpr int TradeIncome = 101;
constexpr int TributeToFirst = 102;      // tribute given to player 1; 103 player 2 ...
constexpr int KillValueFirst = 110;      // value of player 1's units killed ...
constexpr int RazingsFirst = 118;        // player 1's buildings razed ...
constexpr int RazingValueFirst = 126;    // value of player 1's buildings razed ...
constexpr int UnitsLostToFirst = 136;    // own units player 1 killed ...
constexpr int BuildingsLostToFirst = 144;
constexpr int ValueLost = 152;
constexpr int BuildingValueLost = 153;
constexpr int UnitsLost = 154;
constexpr int BuildingsLost = 155;
constexpr int TributeFromFirst = 156;    // tribute received from player 1 ...
constexpr int UnitValueOwned = 164;
constexpr int BuildingValueOwned = 165;
constexpr int FoodCollected = 166;       // 167 wood, 168 stone, 169 gold
constexpr int MilitaryValue = 170;
constexpr int TributeReceived = 171;
constexpr int RazingValue = 172;
constexpr int CastlesBuilt = 173;
constexpr int WondersBuilt = 174;
constexpr int TributeGiven = 175;
constexpr int MonumentValue = 184;       // Castles and Wonders finished
} // namespace Res

// Builds the table, and marks every seat as one that will survive (r45 = 1).
// Before anything is placed or researched.
void Setup(entt::registry& registry);

// Once the world is built and every seat's units placed: the populations
// counted and the history's first reading taken (ecs/Timeline).
void Start(entt::registry& registry);

// Moves the game clock on, at the start of each tick, so that whatever
// happens in the tick is stamped with its time.
void Advance(entt::registry& registry, int32_t dt_ms);

// The end of each tick: the civilian and military population counted, the
// map explored once a game second, survival, then every sheet worked out.
void Update(entt::registry& registry);

// The player's sheet, or null if there is no such seat or no table.
const Sheet* Of(const entt::registry& registry, Core::PlayerId player);

// The F4 overlay's "/team" figure: the player's total averaged
// with those of every player it and that player both regard as allies and
// who, like it, pursues an allied victory -- integer division; the player's
// own total when it does not.
int32_t TeamAverage(const entt::registry& registry, Core::PlayerId player);

// What a unit or building is worth: the paid slots of its price in its
// owner's own table.
int Value(const entt::registry& registry, Core::PlayerId owner, int unit_id);

// --- Where the counters are kept. Each is called by the module the event
// happens in, at the moment it happens. ---

// A unit comes into the world -- trained, or placed at the start. Its value
// goes to r98 and r164.
void UnitCreated(entt::registry& registry, Core::PlayerId owner, int unit_id);

// A building is finished, or placed finished. A Castle or Wonder adds its
// value to r184 and counts in r173 or r174; any other building that is no
// other building's annex and stacks nothing adds it to r98 and r165.
void BuildingFinished(entt::registry& registry, Core::PlayerId owner, int unit_id);

// A unit begins to die, or a building comes down, for whatever reason. Its
// value leaves r98 and r164 (a unit) or r98 and, but for a Castle or Wonder,
// r165 (a finished building) -- each kept from going below zero, on the step
// from alive to dying. A site that was never finished takes nothing away.
void Removed(entt::registry& registry, entt::entity entity);

// `victim` is dying of an attack by `attacker_player` (-1 when nobody's).
// Nothing is counted for a Gaia victim, a wall, or an annex. A unit (or an
// unpacked trebuchet): r20 and the per-victim r69.. for the attacker, r154
// and the per-attacker r136.. for the victim; a building: r43 and r118..,
// r155 and r144... Either way its value goes to the attacker's r170 and the
// victim's r152; a building's also to r172 and r153; and to the per-victim
// r110.. or r126...
void Killed(entt::registry& registry, entt::entity victim, int attacker_player);

// A monk of `converter` takes `target`, still its old owner's: r154 and the
// target's value off r98 for the loser, r41 and the value on r170 for the
// converter.
void Converted(entt::registry& registry, entt::entity target, Core::PlayerId converter);

// A technology is researched: one researched at a building -- with
// a research location and a button -- counts in r21 and adds its price, the
// paid slots of the player's own, to r99. An age records its time.
void Researched(entt::registry& registry, Core::PlayerId player, int tech_id);

// Food, wood, stone or gold brought in -- gathered and dropped off, a trade
// load, a relic's gold -- also counts as collected (r166..r169).
void Collected(Players::Player& player, int dat_resource, Core::Fixed32 amount);

// A relic's gold: collected, and r100.
void RelicGold(Players::Player& player, Core::Fixed32 amount);

// A tribute of `amount` given: r175 for the giver, r171 and the per-giver
// r156.. for the recipient.
void Tributed(entt::registry& registry, Core::PlayerId from, Core::PlayerId to, int amount);

} // namespace ECS::Score
