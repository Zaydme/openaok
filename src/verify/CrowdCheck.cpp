#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "ecs/Animals.h"
#include "ecs/Navigation.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
using Core::Fixed32;
constexpr int kTicksPerSecond = 1000 / 16;

} // namespace

// Eight villagers sent at one sheep: as many as there is room for round it
// work there side by side, and the rest stand by for a spot rather than
// piling on or giving up. Two may pass over each other on their way; none
// stays over another.
bool CheckCrowd() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    std::vector<entt::entity> villagers;
    for (auto e : reg.view<C::Worker, C::Owner>()) {
        if (reg.get<C::Owner>(e).player_id == 1 && reg.get<C::Worker>(e).gatherer < 0) villagers.push_back(e);
    }
    entt::entity sheep = entt::null;
    if (villagers.empty()) {
        verdict = "no villagers";
    } else {
        const auto home = reg.get<C::Transform>(villagers[0]).position;
        while (villagers.size() < 8) {
            villagers.push_back(ECS::Units::Spawn(reg, cfg.GetUnitIdByName("VMBAS"), 1, home));
        }
        Fixed32 nearest(1000);
        for (auto e : reg.view<C::Transform, C::Identity>()) {
            if (!ECS::Animals::Herdable(reg, e)) continue;
            const auto& at = reg.get<C::Transform>(e).position;
            const Fixed32 d = Core::FixedMath::Distance(at.x - home.x, at.y - home.y);
            if (d < nearest) {
                nearest = d;
                sheep = e;
            }
        }
        if (sheep == entt::null) verdict = "no sheep";
    }
    if (verdict == nullptr) {
        for (auto v : villagers) Core::CommandQueue::Get().PushCommand(Core::GatherCommand{v, sheep, 1});
        const Fixed32 body = ECS::Navigation::RadiusOf(reg, villagers[0]);
        // Once the first is at work, each second: how many seconds running
        // each pair has stood less than half a body apart.
        constexpr int kStaysOver = 3;
        int over[8][8] = {};
        int most_at_work = 0;
        bool begun = false;
        for (int t = 0; t < 40 * kTicksPerSecond && verdict == nullptr; ++t) {
            sim.Update(16);
            int at_work = 0;
            for (auto v : villagers) {
                if (reg.get<C::Worker>(v).working) ++at_work;
            }
            if (at_work > most_at_work) most_at_work = at_work;
            if (at_work > 0) begun = true;
            if (!begun || t % kTicksPerSecond != 0) continue;
            for (size_t i = 0; i < villagers.size(); ++i) {
                for (size_t j = i + 1; j < villagers.size(); ++j) {
                    const auto& a = reg.get<C::Transform>(villagers[i]).position;
                    const auto& b = reg.get<C::Transform>(villagers[j]).position;
                    over[i][j] = Core::FixedMath::Distance(a.x - b.x, a.y - b.y) < body ? over[i][j] + 1 : 0;
                    if (over[i][j] >= kStaysOver) verdict = "two villagers at one sheep stayed over each other";
                }
            }
        }
        if (verdict == nullptr && most_at_work < 3) verdict = "too few villagers got to work on the sheep";
        for (auto v : villagers) {
            const auto& w = reg.get<C::Worker>(v);
            if (verdict == nullptr && w.state == Core::UnitState::Idle) {
                verdict = "a villager with no room at the sheep gave up rather than stand by";
            }
        }
    }
    std::printf("crowd=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
