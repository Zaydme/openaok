#pragma once

namespace ECS::Components {

// Which dat unit this entity is.
//
// Everything the interface has to say about an entity -- its icon, its stats,
// what it costs, what it can produce -- is a lookup keyed on this id, so none
// of it is duplicated in the engine. It is the one field that ties a live
// entity back to the dat's tables.
struct Identity {
    int unit_id = -1;
};

} // namespace ECS::Components
