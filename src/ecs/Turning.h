#pragma once
#include <entt/entt.hpp>

#include "ecs/components/Transform.h"

// Turning to face what a unit acts on, before it acts: a ship before it shoots
// or fishes, a trebuchet before it hurls. Most units turn at once. One whose
// rules give a rotation speed -- 0.1 seconds for a ship, 0.5 for a trebuchet
// -- turns one of its graphic's angles (at least eight to a turn) each that
// many seconds, the shorter way round, and acts only once it faces the target
// to within half an angle. Walking turns it at once whatever its speed.
namespace ECS::Turning {

// Turns `unit` a tick's worth toward `at`. True once it faces it.
bool Toward(entt::registry& registry, entt::entity unit, Components::Vector2D at, int32_t dt_ms);

} // namespace ECS::Turning
