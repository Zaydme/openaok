#pragma once
#include "ai/Interpreter.h"
#include "ai/Script.h"

// The script actions, carried out.
//
// The rules' own memory -- goals, strategic numbers, timers, escrow, the
// random number, taunts -- is written here; everything that changes the
// world is an order through ecs/Orders, given by the executor (ai/Executor)
// with the same criteria the matching can-* fact checks.
//
// Walls, gates and the spy are no-ops.
namespace AI::Actions {

void Run(Turn& turn, const Script::Act& act, bool& disable_self);

// A message from the turn's player to the seats in `to` (a bit each), by
// LANGUAGE.DLL id or as text. A message that is a number is a taunt, which
// every computer player it reaches detects ("taunt-detected").
void Chat(Turn& turn, uint16_t to, int string_id, const std::string& text);

} // namespace AI::Actions
