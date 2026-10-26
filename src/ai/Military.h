#pragma once
#include "ai/Interpreter.h"

// A computer player's soldiers, run for its rules.
//
// The rules decide when to attack -- `attack-now`, which takes
// sn-percent-attack-soldiers percent of the soldiers at home (and
// sn-percent-attack-boats of the warships) into an attack group. The group
// goes at the nearest enemy building the player has seen -- failing that,
// any, seen or not -- and on from target to target until there is none or
// nobody is left.
//
// The rest defend: anything hostile that strikes in town or stands in it
// (within sn-maximum-town-size of home) is met by
// sn-percent-enemy-sighted-response percent of the idle soldiers within
// sn-enemy-sighted-response-distance of it; idle soldiers wandered off are
// called back to the muster point, a few tiles from home toward the middle
// of the map, where every military building also sends what it trains.
namespace AI::Military {

void AttackNow(Turn& turn);
void Run(Turn& turn);

// Whether `unit` is in one of the brain's attack groups.
bool Attacking(const Turn& turn, entt::entity unit);

} // namespace AI::Military
