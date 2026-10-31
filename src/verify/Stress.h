#pragma once
#include "core/GameSetup.h"
#include "ecs/Simulation.h"

// A late game to measure what everything costs: the self-check's stress
// scenario, and --stress, which plays it in the window for --perf.
namespace Verify::Stress {

// Eight computer players on a giant Coastal map, in the Imperial Age with
// high resources and room for 200 each.
Core::GameSetup Setup();

// What Populate put in the world.
struct Forces {
    int army = 0;       // soldiers marching on another town
    int skirmish = 0;   // soldiers in the fights between neighbours
    int fleet = 0;      // galleys
};

// Late-game forces in a world built from Setup: for each player an army of
// knights, archers and pikemen sent at the next player's Town Center, a fleet
// of galleys on the water nearest its town sent at the next player's, watch
// towers round its town, and archers and spearmen fighting the next player's
// half way between their towns.
Forces Populate(ECS::Simulation& sim);

} // namespace Verify::Stress
