#include "verify/Harness.h"

#include <cstdio>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/FixedMath.h"
#include "ecs/Animals.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

namespace C = ECS::Components;
using Core::Fixed32;
constexpr Core::PlayerId kUs = Core::PLAYER_LOCAL;
constexpr int kTicksPerSecond = 1000 / 16;

// The dat's hunt range for a shepherd, box to box, and how far past it one
// may stand from the carcass it takes food from: what a walk that ends on the
// edge of the range falls short by.
const Fixed32 kHuntRange = Fixed32(1) / Fixed32(10);
const Fixed32 kRangeSlack = Fixed32(1) / Fixed32(10);

} // namespace

bool CheckHerding() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    std::string verdict = "ok";
    sim.Update(16);

    std::vector<entt::entity> villagers;
    auto workers = reg.view<C::Worker, C::Owner, C::Transform>();
    for (auto e : workers) {
        if (workers.get<C::Owner>(e).player_id == kUs && workers.get<C::Worker>(e).gatherer < 0) {
            villagers.push_back(e);
        }
    }
    entt::entity sheep = entt::null;
    if (villagers.size() < 3) {
        verdict = "no villagers";
    } else {
        const auto& home = reg.get<C::Transform>(villagers[0]).position;
        Fixed32 nearest(1000);
        auto animals = reg.view<C::Transform, C::Identity>();
        for (auto e : animals) {
            if (!ECS::Animals::Herdable(reg, e)) continue;
            const auto& at = animals.get<C::Transform>(e).position;
            const Fixed32 d = Core::FixedMath::Distance(at.x - home.x, at.y - home.y);
            if (d < nearest) {
                nearest = d;
                sheep = e;
            }
        }
        if (sheep == entt::null) verdict = "no sheep";
    }

    // Three villagers sent at a sheep: they kill it, take its food standing
    // at the carcass, and bring it home.
    if (verdict == "ok") {
        for (size_t i = 0; i < 3; ++i) {
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{villagers[i], sheep, kUs});
        }
        const int before = ECS::Players::Stock(*ECS::Players::Find(reg, kUs), Core::ResourceType::Food);
        int killed_at = -1;
        bool home_with_food = false;
        for (int t = 0; t < 90 * kTicksPerSecond && verdict == "ok"; ++t) {
            sim.Update(16);
            if (killed_at < 0 && !reg.valid(sheep)) killed_at = t;
            for (size_t i = 0; i < 3; ++i) {
                const auto& w = reg.get<C::Worker>(villagers[i]);
                if (w.state != Core::UnitState::Gathering || !w.working) continue;
                if (!reg.valid(w.target_resource)) continue;
                const Fixed32 gap = ECS::Navigation::GapAt(
                    reg, villagers[i], reg.get<C::Transform>(villagers[i]).position, w.target_resource);
                if (gap > kHuntRange + kRangeSlack) verdict = "a shepherd took food from out of reach";
            }
            if (ECS::Players::Stock(*ECS::Players::Find(reg, kUs), Core::ResourceType::Food) > before) {
                home_with_food = true;
            }
        }
        if (verdict == "ok" && (killed_at < 0 || killed_at > 20 * kTicksPerSecond)) {
            verdict = "the villagers did not kill the sheep";
        } else if (verdict == "ok" && !home_with_food) {
            verdict = "no food came home from the sheep";
        }
    }
    std::printf("herding=%s\n", verdict.c_str());
    return verdict == "ok";
}

} // namespace Verify
