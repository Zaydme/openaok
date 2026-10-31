#include "ai/Scouting.h"

#include <algorithm>

#include "ai/Military.h"
#include "ecs/Navigation.h"
#include "ecs/Vision.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Monk.h"
#include "ecs/components/Path.h"
#include "ecs/components/Worker.h"
#include "pathfinding/Pathfinder.h"

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

bool Busy(const entt::registry& registry, entt::entity e) {
    if (Walking(registry, e)) return true;
    const auto* combat = registry.try_get<ECS::Components::Combat>(e);
    return combat != nullptr && combat->state != ECS::Components::CombatState::Idle;
}

int Sn(const Turn& turn, Lang::SN sn) { return turn.mind->sn[static_cast<size_t>(sn)]; }

// The next point it has not explored on the rings round home, or false when
// there is none left.
bool NextPoint(Turn& turn, int& out_x, int& out_y) {
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
        if (ECS::Vision::Explored(*turn.registry, turn.player, x, y)) continue;
        out_x = x;
        out_y = y;
        return true;
    }
    return false;
}

// The unexplored point nearest home for an explore group, ring by ring out
// from home, so that what lies near, such as the walls' perimeters, is seen
// first.
bool NearestUnexplored(Turn& turn, int& out_x, int& out_y) {
    const Physics::Grid& grid = *turn.grid;
    const int limit = std::max(grid.GetWidth(), grid.GetHeight());
    const int hx = Physics::TileOf(turn.census->home.x), hy = Physics::TileOf(turn.census->home.y);
    for (int ring = kRingStep; ring <= limit; ring += kRingStep) {
        for (int b = 0; b < kBearings; ++b) {
            const int x = hx + ring * kCos[b] / 1024;
            const int y = hy + ring * kSin[b] / 1024;
            if (!grid.InBounds(x, y) || !grid.IsWalkable(x, y)) continue;
            if (ECS::Vision::Explored(*turn.registry, turn.player, x, y)) continue;
            out_x = x;
            out_y = y;
            return true;
        }
    }
    return false;
}

// The unexplored water nearest a warship that it can sail to, ring by ring out
// from it.
bool NearestUnexploredWater(Turn& turn, entt::entity ship, int& out_x, int& out_y) {
    const entt::registry& registry = *turn.registry;
    const Physics::Grid& grid = *turn.grid;
    const Physics::Layer layer = ECS::Navigation::LayerOf(registry, grid, ship);
    const int region = ECS::Navigation::RegionOf(registry, grid, ship);
    const auto& at = registry.get<ECS::Components::Transform>(ship).position;
    const int sx = Physics::TileOf(at.x), sy = Physics::TileOf(at.y);
    const int limit = std::max(grid.GetWidth(), grid.GetHeight());
    for (int ring = kRingStep; ring <= limit; ring += kRingStep) {
        for (int b = 0; b < kBearings; ++b) {
            const int x = sx + ring * kCos[b] / 1024;
            const int y = sy + ring * kSin[b] / 1024;
            if (!grid.InBounds(x, y) || Pathfinding::RegionAt(grid, x, y, layer) != region) continue;
            if (ECS::Vision::Explored(registry, turn.player, x, y)) continue;
            out_x = x;
            out_y = y;
            return true;
        }
    }
    return false;
}

void ClaimBoats(Turn& turn) {
    Brain& brain = *turn.brain;
    Census& census = *turn.census;
    for (auto& group : brain.boat_explore_groups) {
        std::erase_if(group, [&](entt::entity e) {
            return std::find(census.boats.begin(), census.boats.end(), e) == census.boats.end();
        });
    }
    // Every warship counts, those exploring already among them.
    const int boats = static_cast<int>(census.boats.size());
    const int least_attack = std::max(1, Sn(turn, Lang::SN::MinimumBoatAttackGroupSize));
    const int attack = std::min((boats + least_attack - 1) / least_attack, Sn(turn, Lang::SN::NumberBoatAttackGroups));
    const int left = boats - least_attack * attack;
    const int least = std::max(1, Sn(turn, Lang::SN::MinimumBoatExploreGroupSize));
    const int wanted = left > 0 ? std::min((left + least - 1) / least, Sn(turn, Lang::SN::NumberBoatExploreGroups)) : 0;
    const int most = std::max(1, Sn(turn, Lang::SN::MaximumBoatExploreGroupSize));
    if (static_cast<int>(brain.boat_explore_groups.size()) > wanted) {
        brain.boat_explore_groups.resize(static_cast<size_t>(std::max(0, wanted)));
    }
    while (static_cast<int>(brain.boat_explore_groups.size()) < wanted) brain.boat_explore_groups.emplace_back();
    const auto taken = [&](entt::entity e) {
        for (const auto& g : brain.boat_explore_groups) {
            if (std::find(g.begin(), g.end(), e) != g.end()) return true;
        }
        return false;
    };
    // No more of them than the attack groups leave over.
    int spare = std::max(0, left);
    for (const auto& group : brain.boat_explore_groups) spare -= static_cast<int>(group.size());
    for (auto& group : brain.boat_explore_groups) {
        for (entt::entity e : census.idle_military) {
            if (static_cast<int>(group.size()) >= most || spare <= 0) break;
            if (std::find(census.boats.begin(), census.boats.end(), e) == census.boats.end()) continue;
            if (taken(e) || Military::Attacking(turn, e)) continue;
            group.push_back(e);
            --spare;
        }
    }
    for (const auto& group : brain.boat_explore_groups) {
        for (entt::entity e : group) {
            std::erase(census.boats, e);
            std::erase(census.military, e);
            std::erase(census.idle_military, e);
        }
    }
}

// A lone explorer to the next point it has not explored, when it is not
// already on its way or fighting.
void Explore(Turn& turn, entt::entity unit) {
    if (Busy(*turn.registry, unit)) return;
    int x = 0, y = 0;
    if (NextPoint(turn, x, y)) {
        Give(*turn.registry, *turn.grid, Core::MoveCommand{unit, Core::Fixed32(x), Core::Fixed32(y), turn.player});
    }
}

} // namespace

void Claim(Turn& turn) {
    Brain& brain = *turn.brain;
    Census& census = *turn.census;
    const entt::registry& registry = *turn.registry;
    // Members gone from the army (fallen, garrisoned, converted) leave their
    // groups.
    for (auto& group : brain.explore_groups) {
        std::erase_if(group, [&](entt::entity e) {
            return std::find(census.military.begin(), census.military.end(), e) == census.military.end();
        });
    }
    // The scout is the first group; the rest are soldiers'.
    const int wanted = std::max(0, Sn(turn, Lang::SN::NumberExploreGroups) - (census.scout != entt::null ? 1 : 0));
    const int most = std::max(1, Sn(turn, Lang::SN::MaximumExploreGroupSize));
    if (static_cast<int>(brain.explore_groups.size()) > wanted) brain.explore_groups.resize(static_cast<size_t>(wanted));
    while (static_cast<int>(brain.explore_groups.size()) < wanted) brain.explore_groups.emplace_back();
    for (auto& group : brain.explore_groups) {
        for (auto it = census.idle_military.begin();
             it != census.idle_military.end() && static_cast<int>(group.size()) < most;) {
            const entt::entity e = *it;
            const bool taken = std::any_of(brain.explore_groups.begin(), brain.explore_groups.end(), [&](const auto& g) {
                return std::find(g.begin(), g.end(), e) != g.end();
            });
            if (taken || registry.any_of<ECS::Components::Worker, ECS::Components::Monk>(e) ||
                Military::InAttackGroup(turn, e)) {
                ++it;
                continue;
            }
            group.push_back(e);
            ++it;
        }
    }
    // Out of the army while they explore.
    for (const auto& group : brain.explore_groups) {
        for (entt::entity e : group) {
            std::erase(census.military, e);
            std::erase(census.idle_military, e);
        }
    }
    ClaimBoats(turn);
}

void Run(Turn& turn) {
    if (turn.census->town_center == entt::null) return;
    // The soldiers' groups, once each is big enough, together to the
    // unexplored point nearest home, when none of them is still on the way or
    // fighting.
    const int least = std::max(1, Sn(turn, Lang::SN::MinimumExploreGroupSize));
    for (const auto& group : turn.brain->explore_groups) {
        if (static_cast<int>(group.size()) < least) continue;
        if (std::any_of(group.begin(), group.end(), [&](entt::entity e) { return Busy(*turn.registry, e); })) continue;
        int x = 0, y = 0;
        if (!NearestUnexplored(turn, x, y)) break;
        if (group.size() == 1) {
            Give(*turn.registry, *turn.grid, Core::MoveCommand{group.front(), Core::Fixed32(x), Core::Fixed32(y), turn.player});
        } else {
            Give(*turn.registry, *turn.grid, Core::FormationMoveCommand{group, Core::Fixed32(x), Core::Fixed32(y),
                                                                       Core::Formation::Line, turn.player});
        }
    }
    const int least_boats = std::max(1, Sn(turn, Lang::SN::MinimumBoatExploreGroupSize));
    for (const auto& group : turn.brain->boat_explore_groups) {
        if (static_cast<int>(group.size()) < least_boats) continue;
        if (std::any_of(group.begin(), group.end(), [&](entt::entity e) { return Busy(*turn.registry, e); })) continue;
        int x = 0, y = 0;
        if (!NearestUnexploredWater(turn, group.front(), x, y)) continue;
        for (entt::entity ship : group) {
            Give(*turn.registry, *turn.grid, Core::MoveCommand{ship, Core::Fixed32(x), Core::Fixed32(y), turn.player});
        }
    }
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
