#pragma once
#include <utility>
#include <vector>

#include "ai/Interpreter.h"

// The computer players' walls: the two perimeters the scripts name, and the
// facts and actions about them (build-wall, build-gate, wall-completed-
// percentage, enable-wall-placement...).
//
// A perimeter is a square ring of tiles about where the player's first Town
// Center stood: perimeter 1 kNearRing tiles out from its middle, perimeter 2
// kFarRing, so that one runs 14 to 20 tiles from it and the other 21 to 30.
// A tile of the ring counts as walled when one of the player's finished walls
// or gates stands on it, or when the ground is closed by something else --
// trees, cliffs, water, buildings -- which the wall need not cross. A wall is
// laid a side's open stretch at a time, as a player drags one; a gate goes in
// the middle of a straight stretch of the player's own wall.
namespace AI::Walls {

constexpr int kNearRing = 14;
constexpr int kFarRing = 21;

// The ring's tiles, in order round it; none for a perimeter other than 1 or 2.
std::vector<std::pair<int, int>> Ring(Turn& turn, int perimeter);

// What share of the ring, in percent, is walled; and what share the player
// has not yet explored.
int CompletedPercent(Turn& turn, int perimeter);
int InvisiblePercent(Turn& turn, int perimeter);

// `wall` is the wall's building: palisade wall, or a stone wall of the line.
bool CanBuildWall(Turn& turn, int perimeter, int wall, bool with_escrow);
bool CanAffordComplete(Turn& turn, int perimeter, int wall);
bool BuildWall(Turn& turn, int perimeter, int wall);
bool CanBuildGate(Turn& turn, int perimeter, bool with_escrow);
bool BuildGate(Turn& turn, int perimeter);

// enable-wall-placement: the player's other buildings keep a tile clear of
// the perimeter's line (ai/Placement).
void Reserve(Turn& turn, int perimeter);

} // namespace AI::Walls
