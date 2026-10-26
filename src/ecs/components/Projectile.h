#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Rules.h"
#include "core/Types.h"
#include "ecs/components/Transform.h"

namespace ECS::Components {

// A missile in flight: an arrow, a javelin, a hunter's spear -- each a unit of
// its own in the dat, with its own speed, arc and graphic.
//
// It flies to a point on the ground, not after its target: a target that
// walks out of the way is missed, and whoever is standing where it lands can
// be hit instead. The shooter's attack is copied in when it is loosed, so an
// arrow still hurts if the archer dies while it is in the air.
struct Projectile {
    entt::entity shooter = entt::null;
    entt::entity target = entt::null;
    Core::PlayerId owner = Core::PLAYER_GAIA;
    int unit_id = -1;

    Vector2D from{};
    Vector2D to{};
    Core::Fixed32 length{};
    Core::Fixed32 travelled{};
    Core::Fixed32 speed{};   // tiles per second
    Core::Fixed32 arc{};     // peak height as a share of the distance flown

    // Whether the shot was on target when loosed: an accuracy roll, made then.
    bool on_target = false;

    Core::Rules::ClassAmount attacks[Core::Rules::kMaxClassEntries];
    int attack_count = 0;
    Core::Fixed32 blast_width{};
    // The dat's blast level: what else a blast hurts. 3 the target alone, 2
    // every unit and building in reach -- the shooter's own side too -- 1 and
    // 0 the trees there as well.
    int blast_level = 3;
};

} // namespace ECS::Components
