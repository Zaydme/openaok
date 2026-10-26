#pragma once
#include "ai/Interpreter.h"

// Exploring, for the rules: while sn-number-explore-groups asks for an
// explore group the scout rides out, and villagers the strategic numbers set
// exploring (ai/Economy picks them) walk; each to the next point it has not
// explored on widening rings round home.
namespace AI::Scouting {

void Run(Turn& turn);

} // namespace AI::Scouting
