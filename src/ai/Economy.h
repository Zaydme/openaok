#pragma once
#include <entt/entt.hpp>

#include "ai/Interpreter.h"

// A computer player's villagers, run for its rules.
//
// The strategic numbers divide them:
//   - sn-percent-civilian-builders (capped by sn-cap-civilian-builders) may
//     build -- at most that many at once, and only while there are
//     foundations to put up;
//   - sn-percent-civilian-explorers, at least sn-minimum-civilian-explorers
//     and at most sn-cap-civilian-explorers, explore (ai/Scouting walks them);
//   - the rest gather, in the shares sn-food-, sn-wood-, sn-gold- and
//     sn-stone-gatherer-percentage set. An idle villager goes to the resource
//     furthest below its share; while one resource has more than its share
//     and another fewer, a villager that has just dropped its load is moved
//     across.
//
// A gatherer takes the resource of its kind that costs it least: the walk
// from it to the nearest drop site counts thrice, the walk to it once, and
// one further from a drop site than the matching sn-maximum-*-drop-distance
// is ignored. Food is a carcass already being cut first, then the herd, the
// forage, deer, farms; the player's sheep are walked in to the Town Center.
// A boar is left alone.
namespace AI::Economy {

void Run(Turn& turn);

// `build`: the site (ai/Placement) and a villager to start it. One
// foundation of a kind waits at a time -- two for farms.
bool Build(Turn& turn, int slot, bool forward);

// A villager to put to a new job near `at`: an idle one if there is any,
// else the nearest one not building, exploring or kept to build forward; for
// a forward building, the nearest forward builder first. Null if there are
// none.
entt::entity PickWorker(const Turn& turn, const ECS::Components::Vector2D& at, bool forward = false);

} // namespace AI::Economy
