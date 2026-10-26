#pragma once
#include <entt/entt.hpp>

namespace ECS::Components {

// Which building this drawable belongs to.
//
// A building is several entities: the dat composes large ones from annex
// pieces, each with its own sprite so the renderer can anchor and depth-sort
// them without a special case. Destroying the building has to take its pieces
// with it, and this is the link that makes that possible.
struct PartOf {
    entt::entity building = entt::null;
};

} // namespace ECS::Components
