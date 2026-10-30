#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kComputer = 2;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kSeconds = 10;

// The first open tile from (x, y) outward with a clear square round it.
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
    return {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(4),
            Core::Fixed32(y) - Core::Fixed32(1) / Core::Fixed32(4)};
}

struct Meeting {
    bool placed = false;
    bool wolf_went = false;    // the wolf set on the other
    bool other_went = false;   // the other set on the wolf
    bool hurt = false;         // the other lost hit points
};

// A wolf on a quiet field in the middle of the map, two tiles from `name` of
// `owner`'s, for ten seconds. A wolf told not to attack starts nothing; the
// owner played by the computer when `computer`.
Meeting Meet(const char* name, Core::PlayerId owner, bool computer, bool wolf_attacks) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[kComputer].computer = computer;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    Meeting out;
    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 4)) return out;
    // Nothing of Gaia's that fights near the field but this wolf.
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
                                                        Core::PLAYER_GAIA, At(x - 2, y), {}, 0);
    const entt::entity other = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(name), owner,
                                                         At(x + 1, y), {}, 0);
    if (wolf == entt::null || other == entt::null) return out;
    out.placed = true;
    if (!wolf_attacks) {
        reg.get<ECS::Components::Combat>(wolf).stance = ECS::Components::Stance::NoAttack;
    }
    const int hp = reg.get<ECS::Components::Health>(other).current_hp;
    for (int t = 0; t < kSeconds * kTicksPerSecond; ++t) {
        sim.Update(16);
        if (!reg.valid(wolf) || !reg.valid(other)) break;
        out.wolf_went |= reg.get<ECS::Components::Combat>(wolf).target == other;
        if (const auto* combat = reg.try_get<ECS::Components::Combat>(other)) {
            out.other_went |= combat->target == wolf;
        }
    }
    out.hurt = !reg.valid(other) || reg.get<ECS::Components::Health>(other).current_hp < hp;
    return out;
}

} // namespace

// What a unit picks a fight with on sight. A wolf goes for a villager beside
// it, and leaves alone a Mill, a battering ram, a Monk and a Trade Cart. A
// soldier of a person's leaves a wolf alone; the computer's goes for it.
bool CheckSearch() {
    const char* verdict = nullptr;
    const Meeting villager = Meet("VMBAS", kUs, false, true);
    const Meeting mill = Meet("MILL", kUs, false, true);
    const Meeting ram = Meet("BTRAM", kUs, false, true);
    const Meeting monk = Meet("MONKX", kUs, false, true);
    const Meeting cart = Meet("TCART", kUs, false, true);
    const Meeting ours = Meet("SPRMN", kUs, false, false);
    const Meeting theirs = Meet("SPRMN", kComputer, true, false);
    if (!villager.placed || !mill.placed || !ram.placed || !monk.placed || !cart.placed ||
        !ours.placed || !theirs.placed) {
        verdict = "no field for a wolf and its neighbour";
    } else if (!villager.wolf_went || !villager.hurt) {
        verdict = "a wolf left a villager beside it alone";
    } else if (mill.wolf_went || mill.hurt) {
        verdict = "a wolf went for a Mill";
    } else if (ram.wolf_went || ram.hurt) {
        verdict = "a wolf went for a battering ram";
    } else if (monk.wolf_went || monk.hurt) {
        verdict = "a wolf went for a Monk";
    } else if (cart.wolf_went || cart.hurt) {
        verdict = "a wolf went for a Trade Cart";
    } else if (ours.other_went) {
        verdict = "a person's spearman went for a wolf that did not attack";
    } else if (!theirs.other_went) {
        verdict = "the computer's spearman left a wolf beside it alone";
    }
    std::printf("search=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
