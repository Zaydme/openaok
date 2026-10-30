#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr int kTicksPerSecond = 1000 / 16;
// Two looks round at the longest a wolf waits between them.
constexpr int kSeconds = 15;
constexpr int kHardest = 0;
constexpr int kEasy = 3;

bool Clearing(const Physics::Grid& grid, int& x, int& y, int half) {
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
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2),
            Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)};
}

// Whether a wolf on a quiet field goes for `name` of ours standing `tiles`
// from it, at `difficulty`, within fifteen seconds; -1 when there was no
// field.
int GoesFor(const char* name, int tiles, int difficulty) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.difficulty = difficulty;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 8)) return -1;
    std::vector<entt::entity> wild;
    auto view = reg.view<const ECS::Components::Combat, const ECS::Components::Owner,
                         const ECS::Components::Transform>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id != Core::PLAYER_GAIA) continue;
        const auto& at = view.get<const ECS::Components::Transform>(e).position;
        const int dx = at.x.ToInt() - x, dy = at.y.ToInt() - y;
        if (dx * dx + dy * dy < 30 * 30) wild.push_back(e);
    }
    for (entt::entity e : wild) reg.destroy(e);
    const entt::entity wolf = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("WOLFX"),
                                                        Core::PLAYER_GAIA, At(x - tiles / 2, y), {}, 0);
    const entt::entity other = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(name), kUs,
                                                         At(x - tiles / 2 + tiles, y), {}, 0);
    if (wolf == entt::null || other == entt::null) return -1;
    if (auto* combat = reg.try_get<ECS::Components::Combat>(other)) {
        combat->stance = ECS::Components::Stance::NoAttack;
    }
    for (int t = 0; t < kSeconds * kTicksPerSecond; ++t) {
        sim.Update(16);
        if (!reg.valid(wolf) || !reg.valid(other)) break;
        if (reg.get<ECS::Components::Combat>(wolf).target == other) return 1;
    }
    return 0;
}

} // namespace

// How near a wolf lets people come before it goes for them. At Easy, the
// lobby's own choice, it goes for a villager within half its twelve tiles of
// sight and lets one nine tiles off be; anything else only within a third,
// four tiles. At Hardest it goes for a villager anywhere it sees one.
bool CheckHunt() {
    const char* verdict = nullptr;
    const int near_villager = GoesFor("VMBAS", 5, kEasy);
    const int far_villager = GoesFor("VMBAS", 9, kEasy);
    const int far_hardest = GoesFor("VMBAS", 9, kHardest);
    const int near_soldier = GoesFor("SPRMN", 3, kEasy);
    const int far_soldier = GoesFor("SPRMN", 6, kHardest);
    if (near_villager < 0 || far_villager < 0 || far_hardest < 0 || near_soldier < 0 || far_soldier < 0) {
        verdict = "no field for a wolf";
    } else if (near_villager != 1) {
        verdict = "a wolf left a villager five tiles off alone";
    } else if (far_villager != 0) {
        verdict = "a wolf at Easy went for a villager nine tiles off";
    } else if (far_hardest != 1) {
        verdict = "a wolf at Hardest left a villager nine tiles off alone";
    } else if (near_soldier != 1) {
        verdict = "a wolf left a spearman three tiles off alone";
    } else if (far_soldier != 0) {
        verdict = "a wolf went for a spearman six tiles off";
    }
    std::printf("hunt=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
