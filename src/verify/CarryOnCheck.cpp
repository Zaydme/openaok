#include "verify/Harness.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Mortality.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kSoldiers = 10;

} // namespace

// Soldiers set on an enemy that falls before they reach it carry on to where
// it fell, rather than stopping where their walks were going: the enemy
// moved off six tiles and was struck down as they came.
bool CheckCarryOn() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = nullptr;

    // Open ground fourteen tiles every way.
    int ox = 0, oy = 0;
    for (int y = 16; y < grid.GetHeight() - 16 && ox == 0; ++y) {
        for (int x = 16; x < grid.GetWidth() - 16; ++x) {
            bool clear = true;
            for (int dy = -14; dy <= 14 && clear; ++dy) {
                for (int dx = -14; dx <= 14; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    double mean = 0;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const int spear = cfg.GetUnitIdByName("SPRMN");
        const entt::entity foe = ECS::Units::Spawn(reg, spear, 2, {Core::Fixed32(ox + 8), Core::Fixed32(oy - 3)});
        reg.get<C::Combat>(foe).stance = C::Stance::NoAttack;
        std::vector<entt::entity> soldiers;
        for (int i = 0; i < kSoldiers; ++i) {
            soldiers.push_back(ECS::Units::Spawn(reg, spear, 1, {Core::Fixed32(ox - 10 + i % 5), Core::Fixed32(oy - 3 + i / 5)}));
            Core::CommandQueue::Get().PushCommand(Core::AttackCommand{soldiers.back(), foe, 1});
        }
        for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
        // Off six tiles, and struck down there.
        auto& at = reg.get<C::Transform>(foe);
        at.position = at.previous_position = {Core::Fixed32(ox + 8), Core::Fixed32(oy + 3)};
        ECS::Mortality::Kill(reg, grid, foe);
        for (int t = 0; t < 20 * kTicksPerSecond; ++t) sim.Update(16);
        for (auto e : soldiers) {
            const auto& p = reg.get<C::Transform>(e).position;
            mean += std::hypot(p.x.ToFloat() - (ox + 8), p.y.ToFloat() - (oy + 3)) / kSoldiers;
        }
        if (mean > 2.5) verdict = "soldiers whose target fell before they reached it stopped short of where it fell";
    }
    std::printf("carryon=%s mean=%.1f\n", verdict == nullptr ? "ok" : verdict, mean);
    return verdict == nullptr;
}

} // namespace Verify
