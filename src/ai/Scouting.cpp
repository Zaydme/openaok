#include "ai/Scouting.h"

#include "ecs/Vision.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Path.h"
#include "ecs/components/Worker.h"

namespace AI::Scouting {

namespace {

constexpr int kBearings = 16;
constexpr int kCos[kBearings] = {1024, 946, 724, 392, 0, -392, -724, -946,
                                 -1024, -946, -724, -392, 0, 392, 724, 946};
constexpr int kSin[kBearings] = {0, 392, 724, 946, 1024, 946, 724, 392,
                                 0, -392, -724, -946, -1024, -946, -724, -392};
constexpr int kRingStep = 8;

bool Walking(const entt::registry& registry, entt::entity e) {
    const auto* path = registry.try_get<ECS::Components::Path>(e);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size();
}

// The next point on the ring it has not explored; a ring all seen, the next.
void Explore(Turn& turn, entt::entity unit) {
    entt::registry& registry = *turn.registry;
    if (Walking(registry, unit)) return;
    if (const auto* combat = registry.try_get<ECS::Components::Combat>(unit)) {
        if (combat->state != ECS::Components::CombatState::Idle) return;
    }
    Brain& brain = *turn.brain;
    const Physics::Grid& grid = *turn.grid;
    const int limit = std::max(grid.GetWidth(), grid.GetHeight());
    for (int tries = 0; tries < kBearings * 4; ++tries) {
        const int b = brain.scout_bearing;
        brain.scout_bearing = (brain.scout_bearing + 3) % kBearings;
        if (brain.scout_bearing < 3) brain.scout_ring += kRingStep;
        if (brain.scout_ring > limit) brain.scout_ring = 12;
        const int x = Physics::TileOf(turn.census->home.x) + brain.scout_ring * kCos[b] / 1024;
        const int y = Physics::TileOf(turn.census->home.y) + brain.scout_ring * kSin[b] / 1024;
        if (!grid.InBounds(x, y) || !grid.IsWalkable(x, y)) continue;
        if (ECS::Vision::Explored(registry, turn.player, x, y)) continue;
        Give(registry, *turn.grid, Core::MoveCommand{unit, Core::Fixed32(x), Core::Fixed32(y), turn.player});
        return;
    }
}

} // namespace

void Run(Turn& turn) {
    if (turn.census->town_center == entt::null) return;
    if (turn.census->scout != entt::null &&
        turn.mind->sn[static_cast<size_t>(Lang::SN::NumberExploreGroups)] > 0) {
        Explore(turn, turn.census->scout);
    }
    for (entt::entity v : turn.brain->explorers) {
        const auto& w = turn.registry->get<ECS::Components::Worker>(v);
        if (w.state != Core::UnitState::Idle && w.state != Core::UnitState::Moving) continue;
        Explore(turn, v);
    }
}

} // namespace AI::Scouting
