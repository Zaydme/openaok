#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/GameSetup.h"
#include "core/Types.h"

// Where each player stands with each other: ally, neutral or enemy.
//
// A stance is one player's toward another, and the two need not agree: a
// player may regard another as an ally while that one regards it as an
// enemy.
//
// - Ally: its units defend the ally's units and buildings as their own, and
//   cannot attack them.
// - Neutral: its units ignore the neutral's units and buildings unless
//   ordered to attack, and defend themselves if attacked.
// - Enemy: its units attack the enemy's units and buildings that enter
//   their sight.
//
// The lobby sets them: team-mates allied, with allied victory chosen, and
// everyone else an enemy. Lock Teams fixes them for the game. The values are
// those the diplomacy order carries: 0 ally, 1 neutral, 3 enemy.
//
// Simulation state in the registry's context, like the players' table.
namespace ECS::Diplomacy {

enum class Stance : uint8_t { Ally = 0, Neutral = 1, Enemy = 3 };

struct Table {
    // stance[from][to]: how `from` regards `to`.
    Stance stance[Core::kMaxPlayers][Core::kMaxPlayers] = {};
    // Whether each player pursues an allied victory: wins with its allies.
    bool allied_victory[Core::kMaxPlayers] = {};
    // Lock Teams: no stance, nor allied victory, changes during the game.
    bool locked = false;
};

// From the lobby: team-mates allies with allied victory, the rest enemies.
void Setup(entt::registry& registry, const Core::GameSetup& setup);

// How `from` regards `to`. A player is its own ally; Gaia is everyone's
// enemy and everyone Gaia's -- which of Gaia's things are fought is the
// combat module's business (a wolf is, a deer is not).
Stance StanceOf(const entt::registry& registry, Core::PlayerId from, Core::PlayerId to);

// Both regard each other as allies.
bool Allied(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b);

// Whether the player pursues an allied victory.
bool AlliedVictory(const entt::registry& registry, Core::PlayerId player);

// Lock Teams: stances are the lobby's for the whole game.
bool Locked(const entt::registry& registry);

// Changes how `player` regards `other`. False, changing nothing, when teams
// are locked, or either is not a player in the game, or they are one.
bool SetStance(entt::registry& registry, Core::PlayerId player, Core::PlayerId other,
               Stance stance);

// Turns the player's allied victory on or off. False when teams are locked.
bool SetAlliedVictory(entt::registry& registry, Core::PlayerId player, bool on);

// Whether `a` and `b` win together: the same player, or allied each way with
// allied victory chosen by both.
bool SharesVictory(const entt::registry& registry, Core::PlayerId a, Core::PlayerId b);

// The lobby's team bonuses, applied once when the game starts: each team
// member's civ's team bonus effect, once each, for every member of the team.
// A player with no team has none. They stay in effect if a member later
// changes team or is eliminated.
void ApplyTeamBonuses(entt::registry& registry, const Core::GameSetup& setup);

} // namespace ECS::Diplomacy
