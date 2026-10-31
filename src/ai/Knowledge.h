#pragma once
#include "ai/Interpreter.h"

// What a computer player knows of the world: what it sees now, the ground it
// has explored, and -- from each allied computer player whose rules say
// sn-coop-share-information, allied both ways -- what that one sees and has
// explored too.
namespace AI::Knowledge {

// Whether the player knows `e` is there: in its sight, or an ally's.
bool Sees(const Turn& turn, entt::entity e);

// Whether the player, or such an ally, has explored the tile.
bool Explored(const Turn& turn, int x, int y);

} // namespace AI::Knowledge
