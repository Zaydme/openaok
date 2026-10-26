#pragma once
#include <entt/entt.hpp>

#include "core/FixedPoint.h"

namespace ECS::Components {

// A trade cart or cog on a trade route: from its own Market or Dock (`home`)
// to a foreign one (`partner`) with trade goods, and back with the gold the
// route pays.
struct Trader {
    entt::entity home = entt::null;
    entt::entity partner = entt::null;
    // The gold it carries back, paid at the partner, handed in at home.
    Core::Fixed32 gold{};
    bool loaded = false;
    // On the route. An order of any other kind takes it off; the load, if it
    // has one, stays with it.
    bool active = false;
    // What it is when empty -- a full trade cart is the dat's own unit
    // (TCARTF), and turns back into this on handing its gold in.
    int empty_unit = -1;
    // Approaches in a row that found no way to the building.
    int attempts = 0;
};

} // namespace ECS::Components
