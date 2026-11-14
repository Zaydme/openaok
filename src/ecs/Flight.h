#pragma once

#include <entt/entt.hpp>
#include "core/Gorynych.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Health.h"
#include "ecs/components/Mortal.h"

namespace ECS::flight
{
  // Flight is a property of the registered unit, shared by every peer and restored save.
  inline bool isFlying(const entt::registry& registry, entt::entity entity)
  {
    const auto* identity = registry.try_get<Components::Identity>(entity);
    return identity != nullptr && Core::gorynych::isUnit(identity->unit_id) &&
      registry.all_of<Components::Health>(entity) &&
      !registry.any_of<Components::Dying, Components::Corpse>(entity);
  }
}
