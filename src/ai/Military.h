#pragma once
#include "ai/Interpreter.h"

// A computer player's soldiers, run for its rules.
//
// Its land soldiers make attack groups, as the game makes them: once the
// explore groups have theirs, up to sn-number-attack-groups groups, each
// wanting sn-minimum-attack-group-size soldiers and a random few more, at
// most sn-maximum-attack-group-size. An idle group takes free soldiers
// where most stand within sn-group-form-distance of one another, gathers on
// its leader (within sn-attack-group-gather-spacing), and once it has its
// soldiers and has gathered, and the player knows its enemy, attacks: the
// nearest enemy it can see near its leader, else the nearest enemy building
// the player has seen -- across the water by transport if there is none on
// its own ground -- going idle to regather when the target falls. With
// nothing to attack it walks off at random to look. Attacks wait for the
// initial attack delay (sn-initial-attack-delay and its type).
//
// `attack-now` makes groups of its own of sn-percent-attack-soldiers percent
// of the free soldiers, which go at once (and sn-percent-attack-boats of the
// warships into the boat attack group).
//
// The rest defend. A blow struck at the player's things anywhere -- by Gaia's
// too, but not at an explorer, nor, with sn-ignore-attack-group-under-attack,
// at an attack group on its way -- is answered by
// sn-percent-enemy-sighted-response percent (rounded down) of the idle
// soldiers within sn-enemy-sighted-response-distance of it. And anything
// hostile that strikes in town or stands in it
// (within sn-maximum-town-size of home) is met by
// sn-percent-enemy-sighted-response percent of the idle soldiers within
// sn-enemy-sighted-response-distance of it; idle soldiers wandered off are
// called back to the muster point, a few tiles from home toward the middle
// of the map, where every military building also sends what it trains.
namespace AI::Military {

void AttackNow(Turn& turn);
void Run(Turn& turn);

// Whether `unit` is in an attack group on its way to fight, or in any.
bool Attacking(const Turn& turn, entt::entity unit);
bool InAttackGroup(const Turn& turn, entt::entity unit);

// The land soldiers in attack groups on their way to fight.
int AttackingSoldiers(const Turn& turn);

} // namespace AI::Military
