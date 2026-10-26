#pragma once
#include <entt/entt.hpp>

#include "core/Rules.h"
#include "core/Types.h"

// Applying the dat's effects to a player.
//
// An effect is a list of commands, and the command set is small: change a
// unit attribute (set, add or multiply), change a resource, enable or disable
// a unit, upgrade one unit into another, or change a technology's cost, time
// or availability. Every technology, age, civilisation bonus and tech tree in
// the game is some list of these.
//
// The rules change first -- the player's copy of the unit table -- and then
// everything already on the map that the change reaches is brought into line:
// a villager researching Loom gains the hit points, and a Town Center reaching
// the Feudal Age is rebuilt in the Feudal Age's art.
namespace ECS::Effects {

// Applies effect `effect_id` to `player`. Unknown ids do nothing.
void Apply(entt::registry& registry, Core::PlayerId player, int effect_id);

// Applies a single command. Exposed for the techs module and for checks.
void ApplyCommand(entt::registry& registry, Core::PlayerId player,
                  const Core::Rules::EffectCommand& command);

} // namespace ECS::Effects
