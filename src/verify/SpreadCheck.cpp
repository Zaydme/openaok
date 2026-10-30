#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Navigation.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "physics/Grid.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
using Core::Fixed32;
constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// Eight villagers sent together to one point come to stand about it side by
// side, each in a place of its own, not piled on the point; and ten soldiers
// sent there one at a time, as a gather point sends them, each stand in a
// place of their own round it, none over another.
bool CheckSpread() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    std::vector<entt::entity> villagers;
    for (auto e : reg.view<C::Worker, C::Owner>()) {
        if (reg.get<C::Owner>(e).player_id == 1 && reg.get<C::Worker>(e).gatherer < 0) villagers.push_back(e);
    }
    if (villagers.empty()) {
        verdict = "no villagers";
    } else {
        const auto home = reg.get<C::Transform>(villagers[0]).position;
        while (villagers.size() < 8) {
            villagers.push_back(ECS::Units::Spawn(reg, cfg.GetUnitIdByName("VMBAS"), 1, home));
        }
        // Open ground some way off, every tile round it walkable.
        C::Vector2D point{};
        bool found = false;
        for (int r = 8; r < 30 && !found; ++r) {
            for (int d = -r; d <= r && !found; ++d) {
                const int x = home.x.ToInt() + d, y = home.y.ToInt() + r;
                bool open = true;
                for (int oy = -3; oy <= 3 && open; ++oy) {
                    for (int ox = -3; ox <= 3 && open; ++ox) open = grid.IsWalkable(x + ox, y + oy);
                }
                if (open) {
                    point = {Fixed32(x) + Fixed32(1) / Fixed32(2), Fixed32(y) + Fixed32(1) / Fixed32(2)};
                    found = true;
                }
            }
        }
        if (!found) verdict = "no open ground to send them to";
        if (verdict == nullptr) {
            Core::CommandQueue::Get().PushCommand(
                Core::FormationMoveCommand{villagers, point.x, point.y, Core::Formation::Line, 1});
            for (int t = 0; t < 30 * kTicksPerSecond; ++t) sim.Update(16);
            const Fixed32 width = ECS::Navigation::RadiusOf(reg, villagers[0]) * Fixed32(2);
            for (size_t i = 0; i < villagers.size() && verdict == nullptr; ++i) {
                const auto& a = reg.get<C::Transform>(villagers[i]).position;
                if (Core::FixedMath::Distance(a.x - point.x, a.y - point.y) > Fixed32(4)) {
                    verdict = "a villager sent with the rest did not come to the point";
                }
                for (size_t j = i + 1; j < villagers.size() && verdict == nullptr; ++j) {
                    const auto& b = reg.get<C::Transform>(villagers[j]).position;
                    if (Core::FixedMath::Distance(a.x - b.x, a.y - b.y) < width) {
                        verdict = "villagers sent together to a point stand piled on one another";
                    }
                }
            }
        }
    }
    // One at a time, a second apart, to the point: each comes up against
    // whoever stands there and takes the free place nearest it round it.
    if (verdict == nullptr) {
        const auto& start = reg.get<C::Transform>(villagers[0]).position;
        const C::Vector2D point{start.x + Fixed32(4), start.y};
        std::vector<entt::entity> soldiers;
        for (int i = 0; i < 10; ++i) {
            soldiers.push_back(ECS::Units::Spawn(reg, cfg.GetUnitIdByName("SPRMN"), 1,
                                                 {start.x - Fixed32(3), start.y - Fixed32(4) + Fixed32(i)}));
        }
        for (int i = 0; i < 10; ++i) {
            Core::CommandQueue::Get().PushCommand(Core::MoveCommand{soldiers[i], point.x, point.y, 1});
            for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
        }
        for (int t = 0; t < 20 * kTicksPerSecond; ++t) sim.Update(16);
        const Fixed32 width = ECS::Navigation::RadiusOf(reg, soldiers[0]) * Fixed32(2);
        for (size_t i = 0; i < soldiers.size() && verdict == nullptr; ++i) {
            const auto& a = reg.get<C::Transform>(soldiers[i]).position;
            if (Core::FixedMath::Distance(a.x - point.x, a.y - point.y) > Fixed32(3)) {
                verdict = "a soldier sent after the others did not come to the point";
            }
            for (size_t j = i + 1; j < soldiers.size() && verdict == nullptr; ++j) {
                const auto& b = reg.get<C::Transform>(soldiers[j]).position;
                if (Core::FixedMath::Distance(a.x - b.x, a.y - b.y) < width) {
                    verdict = "soldiers sent one at a time to a point stand piled on one another";
                }
            }
        }
    }
    std::printf("spread=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
