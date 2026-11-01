#include "verify/Harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Health.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;

entt::entity Tough(entt::registry& reg, int unit_id, Core::PlayerId player, int x, int y) {
    const entt::entity e = ECS::Units::Spawn(reg, unit_id, player, {Core::Fixed32(x), Core::Fixed32(y)});
    reg.get<C::Health>(e).max_hp = reg.get<C::Health>(e).current_hp = 100000;
    return e;
}

} // namespace

// A soldier sent past an enemy archer pays it no heed on its way, though it
// sees it and is shot at: it walks on to where it was sent (and only then,
// its order done, turns on it).
bool CheckPassing() {
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
    double off = 0;
    bool struck = false;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const entt::entity spear = Tough(reg, cfg.GetUnitIdByName("SPRMN"), 1, ox - 12, oy);
        Tough(reg, cfg.GetUnitIdByName("ARCHR"), 2, ox, oy - 3);
        Core::CommandQueue::Get().PushCommand(Core::MoveCommand{spear, Core::Fixed32(ox + 12), Core::Fixed32(oy), 1});
        // How near it came to where it was sent.
        off = 1e9;
        for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
            sim.Update(16);
            const auto& at = reg.get<C::Transform>(spear).position;
            off = std::min(off, std::hypot(static_cast<double>(at.x.ToFloat()) - (ox + 12), static_cast<double>(at.y.ToFloat()) - oy));
        }
        struck = reg.get<C::Health>(spear).current_hp < reg.get<C::Health>(spear).max_hp;
        if (!struck) verdict = "the archer never shot at the soldier going past";
        else if (off > 1.5) verdict = "a soldier sent past an enemy turned to fight it";
    }
    std::printf("passing=%s off=%.1f struck=%d\n", verdict == nullptr ? "ok" : verdict, off, struck ? 1 : 0);
    return verdict == nullptr;
}

} // namespace Verify
