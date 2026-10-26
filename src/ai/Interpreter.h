#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "ai/Brain.h"
#include "ai/Census.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "ai/World.h"
#include "core/GameSetup.h"
#include "physics/Grid.h"

// Running a computer player's rules: one pass at a time.
//
// A pass tries every enabled rule in load order; a rule whose facts all hold
// has its actions carried out there and then, so a goal one rule sets is what
// the next rule reads. disable-self switches the rule off after its actions
// have run (the rule's other actions still run that once). Wildcard facts
// remember the player they matched in the rule's variables (this-any-enemy
// ...) for its actions, and forget them when the rule is done.
//
// Timers are game time: enable-timer arms one for so many seconds; from then
// it is triggered until disabled.
namespace AI {

// Everything a pass reads and writes.
struct Turn {
    // The world. Null in the language's own checks, where only the facts
    // that need no world -- goals, strategic numbers, timers, the lobby's
    // settings, the seats -- can be true.
    entt::registry* registry = nullptr;
    Physics::Grid* grid = nullptr;
    Brains* brains = nullptr;
    Brain* brain = nullptr;
    const Script::Program* program = nullptr;
    Mind* mind = nullptr;
    World* world = nullptr;
    Census* census = nullptr;
    const Core::GameSetup* setup = nullptr;
    Core::PlayerId player = 0;
    int64_t now_ms = 0;
    // The rule variables: the player each `any-*` wildcard last matched, -1 none.
    int vars[Lang::kAnyWildcards] = {};
    // Rules whose actions ran this pass, for checks and tracing.
    int fired = 0;
};

// One rules pass.
void Pass(Turn& turn);

// Whether the facts of rule `rule` hold now (rule variables reset first).
bool Holds(Turn& turn, size_t rule);

} // namespace AI
