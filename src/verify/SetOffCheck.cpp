#include "verify/Harness.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kSide = 10;
constexpr int kSoldiers = kSide * kSide;

} // namespace

// A hundred soldiers set on one enemy twenty tiles off all set off at once:
// each has its route the tick after the order, and all of them come up to
// the enemy, which walks off across their way as they come.
bool CheckSetOff() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = nullptr;

    // Open ground from the block to the enemy and round it.
    int ox = 0, oy = 0;
    for (int y = 8; y < grid.GetHeight() - 8 && ox == 0; ++y) {
        for (int x = 2; x < grid.GetWidth() - 32; ++x) {
            bool clear = true;
            for (int dy = -7; dy <= 7 && clear; ++dy) {
                for (int dx = -1; dx <= 31; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    int routed = 0, near = 0;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const int spear = cfg.GetUnitIdByName("SPRMN");
        const entt::entity foe = ECS::Units::Spawn(reg, spear, 2, {Core::Fixed32(ox + 29), Core::Fixed32(oy - 4)});
        reg.get<C::Combat>(foe).stance = C::Stance::NoAttack;
        // Struck but never felled, so the crowd about it can be counted.
        auto keep = [&] { reg.get<C::Health>(foe).current_hp = reg.get<C::Health>(foe).max_hp; };
        std::vector<entt::entity> soldiers;
        for (int i = 0; i < kSoldiers; ++i) {
            soldiers.push_back(ECS::Units::Spawn(
                reg, spear, 1, {Core::Fixed32(ox + i % kSide), Core::Fixed32(oy - kSide / 2 + i / kSide)}));
            Core::CommandQueue::Get().PushCommand(Core::AttackCommand{soldiers.back(), foe, 1});
        }
        Core::CommandQueue::Get().PushCommand(Core::MoveCommand{foe, Core::Fixed32(ox + 29), Core::Fixed32(oy + 5), 2});
        sim.Update(16);
        sim.Update(16);
        keep();
        for (auto e : soldiers) {
            const auto* path = reg.try_get<C::Path>(e);
            if (path != nullptr && path->current_waypoint_index < path->waypoints.size()) ++routed;
        }
        for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
            sim.Update(16);
            keep();
        }
        const auto& at = reg.get<C::Transform>(foe).position;
        for (auto e : soldiers) {
            const auto& p = reg.get<C::Transform>(e).position;
            if (std::hypot((p.x - at.x).ToFloat(), (p.y - at.y).ToFloat()) <= 8.0) ++near;
        }
        if (routed < kSoldiers) {
            verdict = "soldiers set on one enemy waited their turn for a route rather than all setting off";
        } else if (near < kSoldiers) {
            verdict = "soldiers set on one enemy did not all come up to it";
        }
    }
    std::printf("setoff=%s routed=%d/%d near=%d\n", verdict == nullptr ? "ok" : verdict, routed, kSoldiers, near);
    return verdict == nullptr;
}

} // namespace Verify
