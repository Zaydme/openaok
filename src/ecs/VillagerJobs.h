#pragma once
#include <string>

#include "core/Types.h"

#include <entt/entt.hpp>

#include "ecs/components/Animation.h"
#include "ecs/components/Worker.h"

// What a villager currently *is*.
//
// Each villager job is a whole separate unit in the dat. A villager cutting
// wood is `VMLUM`, with its own graphics, its own icon and its own name
// ("Lumberjack"); one carrying a hammer is `VMBLD`. The entity keeps its
// identity as a base villager, so the job it is doing right now has to be
// resolved from what it is doing.
//
// The animation system (to pick the graphic set) and the interface (to print
// the name and icon of what is selected) both ask this module.
namespace ECS::Villagers {

// The dat's three-letter job code: VM + LUM, VF + GLD, and so on.
const char* JobCode(Components::VillagerJob job);

// Which job a worker's current activity puts it on.
Components::VillagerJob JobFor(const Components::Worker& worker);

// The job a target calls for, from the dat's task lists: whichever villager
// job unit has a gather or hunt task naming the target's unit or class. A
// hunt task means the target has to be killed before it can be gathered;
// `hunt` says so. Falls back to the resource it holds when no task names it.
Components::VillagerJob JobForTarget(const entt::registry& registry,
                                     entt::entity target, bool& hunt);

// The dat unit name for a job and sex, e.g. "VMLUM".
std::string DatName(Components::VillagerJob job, bool female);

// The dat unit id a villager currently counts as, or -1 if there is none.
int UnitId(Components::VillagerJob job, bool female);

// The unit the interface should show for `entity`: its job unit while it is at
// one, its own identity otherwise.
int DisplayUnit(const entt::registry& registry, entt::entity entity);

} // namespace ECS::Villagers
