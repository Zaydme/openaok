#include "verify/Harness.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kWide = 20;
constexpr int kDeep = 8;
constexpr int64_t kMostSearched = 4000;   // tiles a tick

} // namespace

// An army crowding round one enemy, finding places about it, plans its
// chases again patch by patch over several ticks, not all in one: no tick
// after the order searches more than a few thousand tiles.
bool CheckReplanSpread() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const char* verdict = nullptr;
    int ox = 0, oy = 0;
    for (int y = 12; y < grid.GetHeight() - 12 && ox == 0; ++y) {
        for (int x = 2; x < grid.GetWidth() - 36; ++x) {
            bool clear = true;
            for (int dy = -11; dy <= 11 && clear; ++dy) {
                for (int dx = -1; dx <= 35; ++dx) {
                    if (!grid.IsWalkable(x + dx, y + dy)) { clear = false; break; }
                }
            }
            if (clear) { ox = x; oy = y; break; }
        }
    }
    int64_t worst = 0;
    if (ox == 0) {
        verdict = "no open ground";
    } else {
        const int spear = cfg.GetUnitIdByName("SPRMN");
        const entt::entity foe = ECS::Units::Spawn(reg, spear, 2, {Core::Fixed32(ox + 20), Core::Fixed32(oy)});
        reg.get<C::Combat>(foe).stance = C::Stance::NoAttack;
        for (int i = 0; i < kWide * kDeep; ++i) {
            const entt::entity e = ECS::Units::Spawn(
                reg, spear, 1, {Core::Fixed32(ox + i % kDeep), Core::Fixed32(oy - kWide / 2 + i / kDeep)});
            Core::CommandQueue::Get().PushCommand(Core::AttackCommand{e, foe, 1});
        }
        sim.Update(16);
        for (int t = 0; t < 30 * kTicksPerSecond && reg.valid(foe); ++t) {
            const int64_t before = Pathfinding::AStar::Searched();
            sim.Update(16);
            worst = std::max(worst, Pathfinding::AStar::Searched() - before);
            reg.get<C::Health>(foe).current_hp = reg.get<C::Health>(foe).max_hp;
        }
        if (worst > kMostSearched) verdict = "an army's chases planned again all in one tick";
    }
    std::printf("replanspread=%s worst=%lld\n", verdict == nullptr ? "ok" : verdict, static_cast<long long>(worst));
    return verdict == nullptr;
}

} // namespace Verify
