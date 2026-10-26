#pragma once
#include "ai/Interpreter.h"

// The tactical computer player under the scripts.
//
// The scripts say what -- build a mill, train a villager, attack now, put
// thirty percent of the gatherers on wood -- and these say how: where the
// mill goes, which villager builds it, who gathers what, which soldiers go
// and at what. The strategic numbers the scripts set steer them; the module
// comments say which number does what. Every decision is an order through
// ecs/Orders.
namespace AI::Executor {

// For the actions. Each is true when an order was given, and keeps the
// turn's census in step with it.
bool Train(Turn& turn, int slot);
bool Research(Turn& turn, int tech);
bool Build(Turn& turn, int slot, bool forward);
void AttackNow(Turn& turn);
bool Delete(Turn& turn, int form, bool building);
void Resign(Turn& turn);

// The stockpile and population, read again after an order changed them.
void Refresh(Turn& turn);

// The executor's own work each time the player thinks, after the rules'
// pass: news from the event log, villagers to their jobs, builders to the
// foundations, soldiers and ships to their groups, scouts out.
void Run(Turn& turn);

} // namespace AI::Executor
