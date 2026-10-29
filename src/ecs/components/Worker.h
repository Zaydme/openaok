#pragma once
#include "ecs/components/Animation.h"
#include <entt/entt.hpp>
#include "core/Types.h"
#include "ecs/components/Transform.h"

namespace ECS::Components {

struct Worker {
    // True on the ticks the villager is actually swinging a hammer at a site,
    // as opposed to walking to one. The animation system reads it to play the
    // building action rather than leaving the villager standing idle.
    bool working = false;

    Core::UnitState state = Core::UnitState::Idle;

    // The job this villager is on: which resource it is fetching, and so which
    // graphic set and name it wears. Not the same thing as what it is carrying.
    Core::ResourceType carry_type = Core::ResourceType::Food;

    // The gathering job the current target calls for -- forager, farmer,
    // hunter, shepherd -- chosen from the dat's task lists when the order is
    // given. Food alone does not say which: a farmer gathers at 0.53 a second
    // and a forager at 0.31, each with its own graphics.
    VillagerJob job = VillagerJob::None;

    // What it is actually carrying, one count per resource, kept apart: a
    // villager that has cut ten wood and then mined five gold carries both. A
    // drop site takes only the ones it accepts, leaving the rest in hand.
    int carried[static_cast<int>(Core::ResourceType::COUNT)] = {};

    // Per resource, not across all of them: the cap is on how much wood a
    // villager can shoulder, not on how much it is holding in total.
    int max_carry = 10;

    // Whose rules and drop sites this worker gathers by. A villager is a
    // different dat unit at each job -- a lumberjack, a forager -- and goes by
    // that unit's (-1 here). Anything else that gathers does it as itself: a
    // fishing ship works at its own rate, by its own gather tasks, and takes
    // its catch to the drop sites its own entry lists -- the Dock.
    int gatherer = -1;

    // Fractional progress toward the next whole unit of the resource being
    // gathered.
    //
    // The dat states gather rates as resources per second, and they are not
    // whole numbers -- a lumberjack takes 0.39 wood a second -- so the
    // fraction is carried here and whole units are taken off it as they
    // accumulate.
    Core::Fixed32 gather_progress{};

    // Hit points mended short of a whole one, in thousandths, carried to the
    // next tick: a repairer mends 12.5 a second (VMREP's work rate), a
    // fraction a tick.
    Core::Fixed32 repair_progress{};
    
    entt::entity target_resource = entt::null;
    // The animal it has set combat on to kill for its food: when combat drops
    // it -- there is no way to it -- the hunt is given up.
    entt::entity hunted = entt::null;
    // The standing tree being felled, before its wood is cut (ecs/Felling),
    // and the milliseconds of felling left: entt::null when none.
    entt::entity felling = entt::null;
    int32_t fell_ms = 0;
    // Where `target_resource` stands -- or stood, once it is gone: where the
    // villager looks for another of the same when it has run out or someone
    // else has finished it.
    Vector2D resource_at{};
    entt::entity target_dropoff = entt::null;
    // Sent to `target_dropoff` by an order: it stands there after handing
    // over its load, rather than going back to work.
    bool ordered_drop = false;
    // What it is building -- or, Repairing, mending: a building or a unit.
    entt::entity target_building = entt::null;

    // Failed attempts to reach `target_building`, so a site walled in by its
    // own crew is given up rather than re-routed to forever.
    //
    // No standing spot is reserved: a villager walks to the nearest point on
    // the building's wall from wherever it happens to be, and the crowd sorts
    // itself out by shoving.
    int build_attempts = 0;
};

} // namespace ECS::Components
