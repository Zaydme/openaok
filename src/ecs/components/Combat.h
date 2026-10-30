#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "ecs/components/Transform.h"

namespace ECS::Components {

enum class CombatState : uint8_t {
    Idle,
    Chasing,     // closing on the target
    Attacking,   // in reach, swinging or shooting on its reload
};

// The four stances.
enum class Stance : uint8_t {
    Aggressive,    // takes on anything hostile it sees, and pursues
    Defensive,     // takes on what comes near, pursues a little, returns
    StandGround,   // fights only what is in reach, never moves to
    NoAttack,      // never starts a fight, not even back
};

// Something that fights. Its numbers -- attack and armour by class, reload,
// range, accuracy, missile -- are its owner's rules, read when needed; this is
// only what it is doing about it.
struct Combat {
    CombatState state = CombatState::Idle;
    Stance stance = Stance::Aggressive;
    entt::entity target = entt::null;
    // Given by an order rather than picked up: an ordered target is pursued
    // whatever the stance, and only another order drops it.
    bool ordered = false;

    int32_t reload_ms = 0;   // until the next attack may start
    int32_t strike_ms = -1;  // until the swing in progress lands; -1: none
    int32_t swing_ms = 0;    // attack animation still to play
    int32_t chase_ms = 0;    // until a chase re-plans its route
    int32_t scan_ms = 0;     // until an idle unit looks for targets again
    // Routes to the target that came back empty, one after another: a target
    // across water or walled in is not chased for ever.
    int chase_failures = 0;

    // Where a defensive unit returns to once a fight is over.
    Vector2D post{};
    bool has_post = false;
    // Whoever struck it last: help is called once for each new attacker.
    entt::entity struck_by = entt::null;
};

} // namespace ECS::Components
