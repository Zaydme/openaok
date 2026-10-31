#pragma once
#include "ai/Interpreter.h"

// The computer players' relics: idle Monks fetch the relics lying on ground
// the player has explored, one Monk to a relic and the relics nearest home
// first, while the player has a finished Monastery; a Monk carrying one takes
// it to the nearest of the player's Monasteries. At Easiest they leave relics
// alone.
namespace AI::Relics {

// Before the army is tasked: the errands given, and the Monks on them kept
// out of the army.
void Run(Turn& turn);

// enemy-captured-relics: every relic on the map is held in the Monasteries of
// the player's enemies.
bool EnemyCaptured(const Turn& turn);

} // namespace AI::Relics
