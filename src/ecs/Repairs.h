#pragma once
#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Rules.h"
#include "core/Types.h"

// Villagers mending what has been damaged.
//
// Who may be mended is the dat's: the repairer villager (VMREP, VFREP) has a
// Repair task (action 106) naming no class, which means any building-type
// object -- buildings, and the trebuchet, which the dat makes one -- and one
// each for the ship classes (fishing ship 21, trade cog 2, transport 20,
// warship 22, boarding boat 53), all with target diplomacy 4: one's own, an
// ally's or Gaia's. The dat names no other siege weapon, so a ram or a
// mangonel is not mended.
//
// The rate is the repairer's work rate, 12.5 hit points a second for a
// building; ships and siege weapons mend at a quarter of that, and every
// villager after the first adds half a villager. Each hit point costs half of
// the build cost over the hit points, of each resource, paid out of the
// owner's stockpile as it is mended. A repair the owner cannot pay for stops.
namespace ECS::Repairs {

// Whether `worker` -- a villager -- may mend `target`: something the
// repairer's tasks name, of its own side or Gaia's, finished, still standing
// and short of its hit points.
bool CanRepair(const entt::registry& registry, entt::entity worker, entt::entity target);

// Hit points a second a crew of `crew` of `player`'s repairers mends `target`
// by, all told.
Core::Fixed32 CrewRate(const entt::registry& registry, Core::PlayerId player,
                       entt::entity target, int crew);

// What one hit point of `target` costs, of each resource (by the dat's id;
// zero where it costs none of it).
struct Price {
    Core::Fixed32 amount[4] = {};
};
Price PerHitPoint(const entt::registry& registry, entt::entity target);

// Mends `hit_points` of `target`, charging its owner. Returns how many were
// mended: fewer when the owner ran out of what it costs.
int Mend(entt::registry& registry, entt::entity target, int hit_points);

} // namespace ECS::Repairs
