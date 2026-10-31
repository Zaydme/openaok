#pragma once
#include "ai/Interpreter.h"

// Exploring, for the rules: sn-number-explore-groups asks for explore groups,
// the scout the first and each further one soldiers taken from the army,
// between sn-minimum- and sn-maximum-explore-group-size of them; and villagers
// the strategic numbers set exploring (ai/Economy picks them) walk. The scout
// and the villagers go to the next point unexplored on widening rings round
// home, the soldiers' groups to the unexplored point nearest home.
namespace AI::Scouting {

// Before the army is tasked: the explore groups filled from idle soldiers,
// or given back as fewer are asked for, and their members kept out of the
// army.
void Claim(Turn& turn);

void Run(Turn& turn);

} // namespace AI::Scouting
