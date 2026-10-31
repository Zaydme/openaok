#include "ai/Navy.h"

#include <algorithm>

#include "core/FixedMath.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/DropSites.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

namespace AI::Navy {

namespace {

namespace R = Core::Rules;
using ECS::Components::Vector2D;

int Sn(const Turn& turn, Lang::SN sn) { return turn.mind->sn[static_cast<size_t>(sn)]; }

} // namespace

void Run(Turn& turn) {
    entt::registry& registry = *turn.registry;
    const Census& c = *turn.census;
    for (entt::entity ship : c.idle_fishing_ships) {
        const auto* identity = registry.try_get<ECS::Components::Identity>(ship);
        const R::UnitRules* rules =
            identity != nullptr ? ECS::Players::UnitOf(registry, turn.player, identity->unit_id) : nullptr;
        if (rules == nullptr) continue;
        // Where it can take its catch: its own entry's drop sites.
        std::vector<ECS::Buildings::Bounds> docks;
        const auto& worker = registry.get<ECS::Components::Worker>(ship);
        for (const Census::Site& site : c.buildings) {
            if (!site.complete || !ECS::DropSites::Accepts(site.unit_id, Core::ResourceType::Food, worker.gatherer)) continue;
            docks.push_back(ECS::Buildings::FootprintBounds(
                registry.get<ECS::Components::Transform>(site.entity).position, site.unit_id));
        }
        if (docks.empty()) continue;
        const Physics::Layer layer = ECS::Navigation::LayerOf(registry, *turn.grid, ship);
        const int region = ECS::Navigation::RegionOf(registry, *turn.grid, ship);
        const Vector2D from = registry.get<ECS::Components::Transform>(ship).position;
        entt::entity best = entt::null;
        Core::Fixed32 best_cost(1 << 20);
        auto stock = registry.view<const ECS::Components::Resource, const ECS::Components::Identity,
                                   const ECS::Components::Transform>();
        for (auto e : stock) {
            if (stock.get<const ECS::Components::Resource>(e).current_amount <= 0) continue;
            const int unit = stock.get<const ECS::Components::Identity>(e).unit_id;
            const R::UnitRules* what = R::Unit(unit);
            bool named = false;
            for (int t = 0; t < rules->task_count && !named; ++t) {
                named = rules->tasks[t].action == R::TaskAction::Gather &&
                        rules->tasks[t].Targets(unit, what != nullptr ? what->cls : -1);
            }
            if (!named) continue;
            // Someone else's fish trap is theirs.
            if (const auto* owner = registry.try_get<ECS::Components::Owner>(e)) {
                if (owner->player_id != turn.player && owner->player_id != Core::PLAYER_GAIA) continue;
            }
            const auto& at = stock.get<const ECS::Components::Transform>(e).position;
            Core::Fixed32 drop(1 << 20);
            for (const auto& dock : docks) drop = std::min(drop, dock.DistanceFrom(at));
            // Fish further from a Dock than sn-maximum-fish-boat-drop-distance
            // are left.
            const int limit = Sn(turn, Lang::SN::MaximumFishBoatDropDistance);
            if (limit >= 0 && drop > Core::Fixed32(limit)) continue;
            const Core::Fixed32 cost = drop * Core::Fixed32(3) + Core::FixedMath::Distance(at.x - from.x, at.y - from.y);
            if (cost > best_cost || (cost == best_cost && e >= best)) continue;
            if (!Pathfinding::Approachable(*turn.grid, region, Physics::TileOf(at.x), Physics::TileOf(at.y), layer)) {
                continue;
            }
            best_cost = cost;
            best = e;
        }
        if (best != entt::null) Give(registry, *turn.grid, Core::GatherCommand{ship, best, turn.player});
    }
}

} // namespace AI::Navy
