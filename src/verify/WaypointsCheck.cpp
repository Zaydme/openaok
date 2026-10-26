#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Waypoints.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr int kTicksPerSecond = 1000 / 16;

// An open square of ground, `half` tiles each way of (x, y), near the middle.
bool Open(const Physics::Grid& grid, int& x, int& y, int half) {
    for (int radius = 0; radius < 60; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                bool clear = true;
                for (int oy = -half; oy <= half && clear; ++oy) {
                    for (int ox = -half; ox <= half && clear; ++ox) {
                        if (!grid.IsWalkable(x + dx + ox, y + dy + oy)) clear = false;
                    }
                }
                if (clear) {
                    x += dx;
                    y += dy;
                    return true;
                }
            }
        }
    }
    return false;
}

ECS::Components::Vector2D At(int x, int y) {
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(4),
            Core::Fixed32(y) - Core::Fixed32(1) / Core::Fixed32(4)};
}

Core::Fixed32 Distance(const ECS::Components::Vector2D& a, const ECS::Components::Vector2D& b) {
    return Core::FixedMath::Distance(a.x - b.x, a.y - b.y);
}

Core::WaypointCommand Waypoint(std::vector<entt::entity> units, const ECS::Components::Vector2D& at) {
    return Core::WaypointCommand{std::move(units), at.x, at.y, Core::Formation::Line, kUs};
}

const char* Check() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& queue = Core::CommandQueue::Get();
    queue.PopAllCommands();
    const int militia = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Open(grid, x, y, 8)) return "no open ground";
    const auto start = At(x - 6, y);
    const ECS::Components::Vector2D route[3] = {At(x, y - 5), At(x + 5, y), At(x, y + 5)};
    auto where = [&](entt::entity e) {
        const auto* t = reg.valid(e) ? reg.try_get<ECS::Components::Transform>(e) : nullptr;
        return t != nullptr ? t->position : ECS::Components::Vector2D{Core::Fixed32(-1000), Core::Fixed32(-1000)};
    };
    const Core::Fixed32 near = Core::Fixed32(3) / Core::Fixed32(5);

    // Three points laid at once: walked to in the order they were laid, and
    // the route gone at its end.
    // Soldiers that fight nothing they meet and that nothing -- a wolf -- can
    // kill, so only the walking is measured.
    auto spawn = [&](const ECS::Components::Vector2D& at) {
        const entt::entity e = ECS::Units::Spawn(reg, militia, kUs, at);
        if (auto* combat = reg.try_get<ECS::Components::Combat>(e)) combat->stance = ECS::Components::Stance::NoAttack;
        if (auto* health = reg.try_get<ECS::Components::Health>(e)) health->current_hp = health->max_hp = 30000;
        return e;
    };
    const entt::entity walker = spawn(start);
    for (const auto& p : route) queue.PushCommand(Waypoint({walker}, p));
    int reached = 0;
    for (int t = 0; t < 60 * kTicksPerSecond && reached < 3; ++t) {
        sim.Update(16);
        const auto at = where(walker);
        for (int k = reached + 1; k < 3; ++k) {
            if (Distance(at, route[k]) < near) return "a waypoint was walked to before the one laid before it";
        }
        if (Distance(at, route[reached]) < near) ++reached;
    }
    if (reached < 3) return "a unit did not walk its three waypoints";
    for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
    if (reg.all_of<ECS::Components::Waypoints>(walker)) return "a route walked to its end was kept";

    // Any other order forgets the route: a move part way along.
    queue.PushCommand(Waypoint({walker}, route[0]));
    queue.PushCommand(Waypoint({walker}, route[1]));
    for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
    if (!reg.all_of<ECS::Components::Waypoints>(walker)) return "a unit given waypoints kept none";
    queue.PushCommand(Core::MoveCommand{walker, start.x, start.y, kUs});
    sim.Update(16);
    if (reg.all_of<ECS::Components::Waypoints>(walker)) return "a move did not clear the waypoints";
    for (int t = 0; t < 30 * kTicksPerSecond; ++t) {
        sim.Update(16);
        if (Distance(where(walker), route[1]) < near) return "a unit walked on to a waypoint after a move";
    }
    if (Distance(where(walker), start) > Core::Fixed32(1)) return "a unit did not end its move";

    // A group: each to its place in the formation at every point.
    const entt::entity second = spawn(At(x - 6, y + 1));
    queue.PushCommand(Waypoint({walker, second}, route[0]));
    queue.PushCommand(Waypoint({walker, second}, route[1]));
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) sim.Update(16);
    for (entt::entity e : {walker, second}) {
        if (reg.all_of<ECS::Components::Waypoints>(e)) return "a group's route was not walked to its end";
        if (Distance(where(e), route[1]) > Core::Fixed32(3)) return "a group did not reach its last waypoint";
    }
    if (Distance(where(walker), where(second)) < Core::Fixed32(1) / Core::Fixed32(4)) {
        return "a group stood on one spot rather than in formation";
    }
    return nullptr;
}

} // namespace

// Waypoints: laid with Shift, walked in order, cleared by any other order, a
// group keeping its formation at each. Prints "waypoints=".
bool CheckWaypoints() {
    const char* verdict = Check();
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("waypoints=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
