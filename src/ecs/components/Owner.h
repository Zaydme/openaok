#pragma once
#include "core/Types.h"

namespace ECS::Components {

struct Owner {
    Core::PlayerId player_id = Core::PLAYER_GAIA;
};

// Whose unit table a thing plays by, when that is not its owner's: a unit
// converted goes on with its maker's numbers and art -- and upgrades that side
// researches later. Absent for everything that never changed hands.
struct RulesOf {
    Core::PlayerId player = Core::PLAYER_GAIA;
};

} // namespace ECS::Components
