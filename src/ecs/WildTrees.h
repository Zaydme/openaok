#pragma once

#include <entt/entt.hpp>

namespace ECS
{
  class Simulation;
}

namespace ECS::wild_trees
{
  // Places neutral walking trees and Gorynyches after random-map starting units.
  void populate(Simulation& simulation);
  // A claimed creature becomes a normal soldier, with no further herding.
  void adopt(entt::registry& registry);
}
