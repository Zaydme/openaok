#include "ecs/Resources.h"

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"

namespace ECS::Resources {

void Exhaust(entt::registry& registry, Physics::Grid& grid, entt::entity resource) {
    if (!registry.valid(resource)) return;

    // A farm is a building: its own teardown handles its art, its owner's
    // books and its ground.
    if (registry.all_of<Components::Building>(resource)) {
        Buildings::Destroy(registry, grid, resource);
        return;
    }

    if (const auto* at = registry.try_get<Components::Transform>(resource)) {
        const auto position = at->position;
        const int x = Physics::TileOf(position.x);
        const int y = Physics::TileOf(position.y);
        // A map object blocks the tile it stands on; its remains do not.
        grid.SetWalkable(x, y, true);

        // What it leaves behind, if the dat names anything.
        if (const auto* identity = registry.try_get<Components::Identity>(resource)) {
            const Core::Rules::UnitRules* rules = Core::Rules::Unit(identity->unit_id);
            auto& cfg = Core::ConfigManager::Get();
            const auto remains = rules != nullptr ? cfg.GetObjectArt(rules->dead_unit)
                                                  : Core::ConfigManager::ObjectArt{};
            if (remains.Valid()) {
                const entt::entity stump = registry.create();
                registry.emplace<Components::Transform>(stump, position, position);
                Components::Sprite sprite{};
                sprite.slp_id = remains.slp_id;
                sprite.slp_frame = 0;
                sprite.width = 0;
                sprite.height = 0;
                registry.emplace<Components::Sprite>(stump, std::move(sprite));
            }
        }
    }
    registry.destroy(resource);
}

} // namespace ECS::Resources
