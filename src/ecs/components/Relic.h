#pragma once
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"

namespace ECS::Components {

// A unit sent to fetch a relic, or to put the one it carries away.
struct RelicErrand {
    entt::entity relic = entt::null;       // to pick up
    entt::entity monastery = entt::null;   // to deposit at
    int attempts = 0;
};

// A unit carrying a relic: the relic itself, out of the world while carried,
// and what the unit was before it picked it up.
struct RelicBearer {
    entt::entity relic = entt::null;
    int was_unit = -1;
};

// A building holding relics, which earn its owner gold.
struct RelicShrine {
    std::vector<entt::entity> relics;
    Core::Fixed32 gold{};   // earned and not yet whole
};

} // namespace ECS::Components
