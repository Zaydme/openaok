#pragma once
#include "ai/Interpreter.h"

// Exploring, for the rules: sn-number-explore-groups asks for explore groups,
// the scout the first and each further one soldiers taken from the army,
// between sn-minimum- and sn-maximum-explore-group-size of them; and villagers
// the strategic numbers set exploring (ai/Economy picks them) walk. The scout
// and the villagers go to the next point unexplored on widening rings round
// home, the soldiers' groups to the unexplored point nearest home. Warships
// the boat attack groups leave over -- once each attack group has its
// sn-minimum-boat-attack-group-size -- explore the water in up to
// sn-number-boat-explore-groups groups, no more of them than are left over,
// each to the unexplored water nearest it. The scout and the groups, idle at
// a point, go on to the next once sn-consecutive-idle-unit-limit seconds
// have passed since they were sent to it.
namespace AI::Scouting {

// Before the army is tasked: the explore groups filled from idle soldiers,
// or given back as fewer are asked for, and their members kept out of the
// army.
void Claim(Turn& turn);

void Run(Turn& turn);

// The unexplored water nearest a warship that it can sail to, ring by ring
// out from it; false if there is none.
bool UnexploredWater(Turn& turn, entt::entity ship, int& x, int& y);

} // namespace AI::Scouting
