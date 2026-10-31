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
// soldiers and has gathered, and the player knows its enemy, attacks what the
// game's target evaluation chooses of the enemy things the player knows --
// across the water by transport if there is none on its own ground -- going
// idle to regather when the target falls. With
// nothing to attack it walks off at random to look. Attacks wait for the
// initial attack delay (sn-initial-attack-delay and its type).
//
// Its warships make boat attack groups the same way, of up to
// sn-number-boat-attack-groups groups of the boat sizes; one whose player
// knows no enemy explores the water instead.
//
// `attack-now` makes groups of its own of sn-percent-attack-soldiers percent
// of the free soldiers (and sn-percent-attack-boats of the free warships),
// which go at once.
//
// The rest defend. A blow struck at the player's things anywhere -- by Gaia's
// too, but not at an explorer, nor, with sn-ignore-attack-group-under-attack,
// at an attack group on its way -- is answered by
// sn-percent-enemy-sighted-response percent (rounded down) of the idle
// soldiers within sn-enemy-sighted-response-distance of it. And anything
// hostile that strikes in town or stands in it
// (within sn-maximum-town-size of home) is met by
// sn-percent-enemy-sighted-response percent of the idle soldiers within
// sn-enemy-sighted-response-distance of it. With sn-gather-defense-units,
// the idle soldiers in no group are gathered at the Town Center once a
// minute while the town is quiet.
//
// Allied computer players help one another (sn-coop-share-attacking): a
// player struck by another's calls on them at most every
// sn-coop-share-attacking-interval seconds, and each, its attacks allowed,
// goes for the striker's things first and sends its attack groups with what
// they have. What they see they share (ai/Knowledge).
namespace AI::Military {

void AttackNow(Turn& turn);
void Run(Turn& turn);

// Whether `unit` is in an attack group on its way to fight, or in any.
bool Attacking(const Turn& turn, entt::entity unit);
bool InAttackGroup(const Turn& turn, entt::entity unit);

// The land soldiers in attack groups on their way to fight, and the warships
// in boat attack groups attacking.
int AttackingSoldiers(const Turn& turn);
int AttackingBoats(const Turn& turn);

} // namespace AI::Military
