#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "ecs/Combat.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Health.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kUs = 1;
constexpr Core::PlayerId kThem = 2;
constexpr int kTicksPerSecond = 1000 / 16;

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

Core::Fixed32 Apart(const entt::registry& reg, entt::entity a, ECS::Components::Vector2D q) {
    const auto& p = reg.get<ECS::Components::Transform>(a).position;
    return Core::FixedMath::Distance(p.x - q.x, p.y - q.y);
}

struct Answer {
    bool placed = false;
    bool fought = false;        // the forager set on the attacker
    bool ran = false;           // it got a tile further from the attacker
    bool back = false;          // it is gathering the bush again by the end
    bool neighbour_came = false;   // the idle villager beside it went for the attacker
    bool soldier_came = false;     // the idle spearman beside it did
};

// A forager of ours at a berry bush on a quiet field, an idle villager and an
// idle spearman of ours two tiles off, struck once by `attacker_name` of
// `attacker_owner`'s, which then does nothing more; twenty seconds after.
// The attacker has one hit point, so a forager that fights it kills it.
Answer Struck(const char* attacker_name, Core::PlayerId attacker_owner) {
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    Answer out;
    int x = grid.GetWidth() / 2;
    int y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 6)) return out;
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
    auto place = [&](const char* name, Core::PlayerId owner, int dx, int dy) {
        return ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(name), owner, At(x + dx, y + dy), {}, 0);
    };
    const entt::entity bush = place("FORAG", Core::PLAYER_GAIA, 0, 0);
    const entt::entity forager = place("VMBAS", kUs, 1, 0);
    const entt::entity idle = place("VMBAS", kUs, 1, 2);
    const entt::entity guard = place("SPRMN", kUs, 1, -2);
    if (bush == entt::null || forager == entt::null || idle == entt::null || guard == entt::null) {
        return out;
    }
    ECS::Orders::Execute(reg, grid, Core::GatherCommand{forager, bush, kUs});
    for (int t = 0; t < 4 * kTicksPerSecond; ++t) sim.Update(16);
    // The attacker comes once the forager is at work, and strikes.
    const entt::entity attacker = place(attacker_name, attacker_owner, 2, 1);
    if (attacker == entt::null) return out;
    out.placed = true;
    reg.get<ECS::Components::Combat>(attacker).stance = ECS::Components::Stance::NoAttack;
    reg.get<ECS::Components::Health>(attacker).current_hp = 1;
    const ECS::Components::Vector2D struck_from = reg.get<ECS::Components::Transform>(attacker).position;
    const Core::Fixed32 before = Apart(reg, forager, struck_from);
    ECS::Combat::Hurt(reg, grid, forager, Core::Fixed32(1), attacker);
    for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
        sim.Update(16);
        const bool alive = reg.valid(attacker);
        out.fought |= alive && reg.get<ECS::Components::Combat>(forager).target == attacker;
        out.ran |= Apart(reg, forager, struck_from) >= before + Core::Fixed32(1);
        out.neighbour_came |= alive && reg.get<ECS::Components::Combat>(idle).target == attacker;
        out.soldier_came |= alive && reg.get<ECS::Components::Combat>(guard).target == attacker;
    }
    const auto& worker = reg.get<ECS::Components::Worker>(forager);
    out.back = worker.target_resource == bush && worker.state != Core::UnitState::Idle;
    return out;
}

} // namespace

// What a villager at work does when struck. By a wolf, it breaks off, fights
// it, and goes back to its bush after; an idle villager and an idle spearman
// beside it come to help. By another player's soldier, it runs a few steps
// off and goes back, fighting nothing; the spearman comes, the villager does
// not.
bool CheckAnswer() {
    const char* verdict = nullptr;
    const Answer wolf = Struck("WOLFX", Core::PLAYER_GAIA);
    const Answer soldier = Struck("SPRMN", kThem);
    if (!wolf.placed || !soldier.placed) {
        verdict = "no field for the forager";
    } else if (!wolf.fought) {
        verdict = "a forager bitten by a wolf did not fight it";
    } else if (!wolf.back) {
        verdict = "a forager that fought off a wolf did not go back to its bush";
    } else if (!wolf.neighbour_came || !wolf.soldier_came) {
        verdict = "nobody idle beside a forager bitten by a wolf came to help";
    } else if (soldier.fought) {
        verdict = "a forager struck by a soldier fought it";
    } else if (!soldier.ran) {
        verdict = "a forager struck by a soldier did not run";
    } else if (!soldier.back) {
        verdict = "a forager that ran from a soldier did not go back to its bush";
    } else if (soldier.neighbour_came) {
        verdict = "an idle villager went for another player's soldier";
    } else if (!soldier.soldier_came) {
        verdict = "an idle spearman beside a struck forager did not come to help";
    }
    std::printf("answer=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
