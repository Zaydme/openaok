#pragma once
#include <string_view>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "physics/Grid.h"

// Cheat codes, typed into chat. The codes that make a unit give units of the
// dat: the Cobra car (COBRA, 748) and the Saboteur (HDSQD, 706).
//
// The interface recognises a chat line as a cheat (Recognise) and sends it as
// a CheatCommand; the code is carried out inside the tick (Apply), for the
// player who typed it, and only when the lobby allowed cheats
// (GameSetup::cheats, string 13526 "Allow Cheats").
namespace ECS::Cheats {

enum Code : int {
    kNone = 0,
    kFood,            // "cheese steak jimmy's": 1,000 food
    kWood,            // "lumberjack": 1,000 wood
    kGold,            // "robin hood": 1,000 gold
    kStone,           // "rock on": 1,000 stone
    kMarco,           // "marco": the whole map explored (again to undo)
    kPolo,            // "polo": no fog of war (again to undo)
    kAegis,           // "aegis": building, gathering and training at once
    kNaturalWonders,  // "natural wonders": command nature's animals
    kWinner,          // "i r winner": win the game
    kBlackDeath,      // "black death": every enemy destroyed
    kWimpy,           // "wimpywimpywimpy": destroy yourself
    kCobra,           // "how do you turn this on": a Cobra car
    kSmithereens,     // "to smithereens": a Saboteur
    // "torpedo1" .. "torpedo8": player n destroyed -- kTorpedo + n.
    kTorpedo = 100,
};

// The cheat a chat line is, or kNone. Case and the spaces around it do not
// matter; the words do.
int Recognise(std::string_view chat);

// Carries out `code` for `player`. False -- and nothing done -- when the lobby
// did not allow cheats or the code is not one.
bool Apply(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player, int code);

// Whether `player` has "aegis" on: its buildings go up, its villagers fill
// their loads and its buildings finish what they make, at once.
bool Aegis(const entt::registry& registry, Core::PlayerId player);

// Whether `player` commands nature's animals ("natural wonders").
bool CommandsNature(const entt::registry& registry, Core::PlayerId player);

} // namespace ECS::Cheats
