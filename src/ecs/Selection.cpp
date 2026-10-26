#include "ecs/Selection.h"

#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"

namespace ECS::Selection {

entt::entity Commanded(const entt::registry& registry, Core::PlayerId player) {
    auto view = registry.view<const Components::Selectable,
                              const Components::Owner>();
    for (auto entity : view) {
        if (!view.get<const Components::Selectable>(entity).is_selected) continue;
        if (view.get<const Components::Owner>(entity).player_id != player) continue;
        return entity;
    }
    return entt::null;
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
