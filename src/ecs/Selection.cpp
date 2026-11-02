#include "ecs/Selection.h"

#include <algorithm>

#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"

namespace ECS::Selection {

namespace {

bool Foundation(const entt::registry& registry, entt::entity entity) {
    const auto* site = registry.try_get<Components::Building>(entity);
    return site != nullptr && site->state != Components::BuildingState::Complete;
}

int KindOf(const entt::registry& registry, entt::entity entity) {
    const auto* id = registry.try_get<Components::Identity>(entity);
    return id != nullptr ? id->unit_id : -1;
}

} // namespace

entt::entity Commanded(const entt::registry& registry, Core::PlayerId player) {
    auto view = registry.view<const Components::Selectable,
                              const Components::Owner>();
    entt::entity first = entt::null;
    for (auto entity : view) {
        if (!view.get<const Components::Selectable>(entity).is_selected) continue;
        if (view.get<const Components::Owner>(entity).player_id != player) continue;
        if (first == entt::null) {
            first = entity;
            if (!Foundation(registry, entity)) return entity;
        } else if (KindOf(registry, entity) == KindOf(registry, first) && !Foundation(registry, entity)) {
            return entity;
        }
    }
    return first;
}

void Alike(const entt::registry& registry, Core::PlayerId player, entt::entity building,
           std::vector<entt::entity>& out) {
    out.clear();
    if (!registry.all_of<Components::Building>(building)) return;
    const int kind = KindOf(registry, building);
    auto view = registry.view<const Components::Selectable, const Components::Owner,
                              const Components::Building>();
    for (auto entity : view) {
        if (entity == building || !view.get<const Components::Selectable>(entity).is_selected) continue;
        if (view.get<const Components::Owner>(entity).player_id != player) continue;
        if (KindOf(registry, entity) == kind) out.push_back(entity);
    }
    // EnTT views run newest first.
    std::reverse(out.begin(), out.end());
}

entt::entity Inspected(const entt::registry& registry, Core::PlayerId player) {
    const entt::entity own = Commanded(registry, player);
    if (own != entt::null) return own;

    // Nothing of the player's own: describe whatever else is selected. Map
    // objects carry no owner at all.
    auto view = registry.view<const Components::Selectable>();
    for (auto entity : view) {
        if (view.get<const Components::Selectable>(entity).is_selected) return entity;
    }
    return entt::null;
}

} // namespace ECS::Selection
