#include "ecs/Allegiance.h"

#include <utility>
#include <vector>

#include "core/Rules.h"
#include "ecs/Events.h"
#include "ecs/Garrisons.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/Trade.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Capturable.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Duty.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Path.h"
#include "ecs/components/RallyPoint.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace ECS::Allegiance {

namespace R = Core::Rules;

namespace {

// The dat's classes of what Gaia keeps whoever sees it: artifacts (1), prey
// (9) and predators (10), miscellaneous objects (11), terrain (14), birds
// (38), salvage (40) and resource (41) heaps, relics (42), and livestock
// (58), which is herded instead (ecs/Animals).
constexpr int kNeverTaken[] = {1, 9, 10, 11, 14, 38, 40, 41, 42, 58};

} // namespace

void Change(entt::registry& registry, const Physics::Grid& grid, entt::entity entity,
            Core::PlayerId player) {
    auto* owner = registry.try_get<Components::Owner>(entity);
    const auto* identity = registry.try_get<Components::Identity>(entity);
    if (owner == nullptr || identity == nullptr || owner->player_id == player) return;
    const Core::PlayerId was = owner->player_id;
    const bool building = registry.all_of<Components::Building>(entity);

    // What it gives its owner goes with it.
    uint8_t modes = R::StorageMode::WhileAlive;
    if (building) {
        Production::Abandon(registry, entity);
        Garrisons::Eject(registry, grid, entity);
        registry.remove<Components::RallyPoint>(entity);
        if (registry.get<Components::Building>(entity).state ==
            Components::BuildingState::Complete) {
            modes |= R::StorageMode::WhileStanding;
        }
    }
    Players::Contribute(registry, was, identity->unit_id, modes, -1);
    owner->player_id = player;
    Players::Contribute(registry, player, identity->unit_id, modes, +1);
    // It goes on playing by the table of the side that made it: its maker's
    // technology, not its new owner's (Components::RulesOf). Back with its
    // maker, it is simply its own again.
    if (!registry.all_of<Components::RulesOf>(entity)) {
        registry.emplace<Components::RulesOf>(entity, was);
    } else if (registry.get<Components::RulesOf>(entity).player == player) {
        registry.remove<Components::RulesOf>(entity);
    }

    // A building's pieces are drawn in their owner's colour.
    if (building) {
        auto parts = registry.view<Components::PartOf, Components::Owner>();
        for (auto part : parts) {
            if (parts.get<Components::PartOf>(part).building == entity) {
                parts.get<Components::Owner>(part).player_id = player;
            }
        }
    }

    // A transport changes sides with those aboard.
    if (!building) {
        if (const auto* aboard = registry.try_get<Components::Garrison>(entity)) {
            const std::vector<entt::entity> inside = aboard->inside;
            for (entt::entity unit : inside) {
                if (registry.valid(unit)) Change(registry, grid, unit, player);
            }
        }
    }

    // Whatever it was doing stops. A trader's route and load were its old
    // owner's.
    registry.remove<Components::Path, Components::Duty, Components::Boarding,
                    Components::Landing>(entity);
    Trade::Forget(registry, entity);
    if (auto* combat = registry.try_get<Components::Combat>(entity)) {
        combat->state = Components::CombatState::Idle;
        combat->target = entt::null;
        combat->ordered = false;
        combat->strike_ms = -1;
        combat->has_post = false;
    }
    if (auto* worker = registry.try_get<Components::Worker>(entity)) {
        worker->state = Core::UnitState::Idle;
        worker->working = false;
    }
    if (auto* selectable = registry.try_get<Components::Selectable>(entity)) {
        selectable->is_selected = false;
    }

    const auto* at = registry.try_get<Components::Transform>(entity);
    Events::Emit(registry, {.kind = Events::Kind::Converted, .subject = entity,
                            .player = player, .id = was,
                            .at = at != nullptr ? at->position : Components::Vector2D{}});
}

bool Takeable(int unit_id) {
    const Core::Rules::UnitRules* rules = Core::Rules::Unit(unit_id);
    if (rules == nullptr) return false;
    for (int cls : kNeverTaken) {
        if (rules->cls == cls) return false;
    }
    return true;
}

void Capture(entt::registry& registry, const Physics::Grid& grid) {
    std::vector<std::pair<entt::entity, Core::PlayerId>> taken;
    auto gaia = registry.view<Components::Capturable, Components::Transform, Components::Owner>();
    for (auto entity : gaia) {
        if (gaia.get<Components::Owner>(entity).player_id != Core::PLAYER_GAIA) continue;
        const auto& at = gaia.get<Components::Transform>(entity).position;
        const int x = Physics::TileOf(at.x);
        const int y = Physics::TileOf(at.y);
        for (Core::PlayerId player = 1; player < Core::kMaxPlayers; ++player) {
            if (Players::Find(registry, player) == nullptr) continue;
            if (!Vision::Visible(registry, player, x, y)) continue;
            taken.emplace_back(entity, player);
            break;
        }
    }
    for (const auto& [entity, player] : taken) {
        registry.remove<Components::Capturable>(entity);
        Change(registry, grid, entity, player);
    }
}

} // namespace ECS::Allegiance
