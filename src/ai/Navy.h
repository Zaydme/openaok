#pragma once
#include "ai/Interpreter.h"

// A computer player's ships, run for its rules. The scripts build the Dock
// (where ai/Placement finds a shore on water of
// sn-minimum-water-body-size-for-dock tiles), train the fishing ships and
// warships -- "petersen fishboat", "petersen warboat", which "petersen map
// loads" loads on the water maps -- and send warships to attack
// (ai/Military). This sends each idle fishing ship to the fish that costs it
// least to work: the walk from it to the nearest Dock counting thrice, the
// sail to it once.
namespace AI::Navy {

void Run(Turn& turn);

} // namespace AI::Navy
