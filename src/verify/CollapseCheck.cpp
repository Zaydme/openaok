#include "verify/Harness.h"

#include <cstdio>

#include "core/ConfigManager.h"
#include "ecs/Buildings.h"
#include "ecs/Mortality.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Mortal.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// The entity playing `graphic` as its fall, or null.
entt::entity Falling(entt::registry& reg, int graphic) {
    auto view = reg.view<ECS::Components::Animation, ECS::Components::Dying>();
    for (auto e : view) {
        if (view.get<ECS::Components::Animation>(e).graphic[static_cast<int>(
                ECS::Components::AnimState::Idle)] == graphic) {
            return e;
        }
    }
    return entt::null;
}

} // namespace

// A building destroyed falls: its dat dying graphic (a House's collapse in
// dust, with the explosion's sound) plays once over the rubble it leaves.
bool CheckCollapse() {
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const char* verdict = nullptr;
    const int house = cfg.GetUnitIdByName("HOUS");
    const auto* rules = ECS::Players::UnitOf(reg, 1, house);
    entt::entity site = entt::null;
    for (int y = 30; y < 90 && site == entt::null; y += 3) {
        for (int x = 30; x < 90 && site == entt::null; x += 3) {
            if (ECS::Buildings::CanPlace(grid, house, x, y)) site = ECS::Buildings::Place(reg, grid, house, 1, x, y, true);
        }
    }
    if (site == entt::null || rules == nullptr || rules->dying_graphic < 0) {
        verdict = "no House to destroy";
    } else {
        ECS::Mortality::Kill(reg, grid, site);
        const entt::entity fall = Falling(reg, rules->dying_graphic);
        if (reg.valid(site)) {
            verdict = "a destroyed House still stands";
        } else if (fall == entt::null) {
            verdict = "a destroyed House did not fall in its dying graphic";
        } else {
            for (int t = 0; t < 5 * kTicksPerSecond; ++t) sim.Update(16);
            if (reg.valid(fall)) verdict = "a House's fall went on playing";
        }
    }
    std::printf("collapse=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
