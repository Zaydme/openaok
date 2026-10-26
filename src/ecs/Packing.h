#pragma once
#include <entt/entt.hpp>

#include "core/Types.h"
#include "physics/Grid.h"

// Siege engines that pack up to move and set up to shoot: the trebuchet.
//
// The dat makes these two units joined by their transform unit (Building.
// TransformUnit): the Trebuchet (TREBU, 42) shoots and cannot move, the
// Trebuchet (Packed) (PTREB, 331) moves and cannot shoot, and each names the
// other. Packing turns the one into the other where it stands, keeping its
// wounds in proportion; while it is at it, it does nothing else. A packed
// trebuchet sent at an enemy -- its task 125, Unpack and Attack -- goes until
// the enemy is in the unpacked trebuchet's range, sets up there and opens
// fire.
//
// How long packing takes is not in the dat: it is kPackMs, below. The packing
// art (TREBU_W*, SLPs 1247-1249) is not on the disc.
namespace ECS::Packing {

constexpr int32_t kPackMs = 4000;

// Whether `entity` packs and unpacks: its rules name a transform unit, and it
// is a unit rather than a building.
bool Packs(const entt::registry& registry, entt::entity entity);

// Whether it is packed now: the dat's Packed Unit class (51).
bool Packed(const entt::registry& registry, entt::entity entity);

// Starts packing (`pack` true) or unpacking a unit that is the other way.
// False if it does not pack, is already that way or is at it.
bool Start(entt::registry& registry, entt::entity unit, bool pack);

// A packed engine sent at `target`: it closes to the unpacked engine's range,
// unpacks, and attacks. False if it cannot.
bool Assault(entt::registry& registry, const Physics::Grid& grid, entt::entity unit,
             entt::entity target);

// Stops a pack or an unpack under way, leaving it as it was, or a packed
// engine's way to set up.
void Cancel(entt::registry& registry, entt::entity unit);

// Whether it is in the middle of packing or unpacking.
bool Busy(const entt::registry& registry, entt::entity unit);

// How far through, in percent; 0 when not at it.
// (For the interface's "Packing Progress", string 42022.)
int ProgressPercent(const entt::registry& registry, entt::entity unit);

// Once a tick.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

} // namespace ECS::Packing
