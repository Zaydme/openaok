#include "verify/Harness.h"
#include "ecs/components/Path.h"
#include "ai/Brain.h"
#include "ecs/Mortality.h"
#include "ecs/components/Identity.h"
#include "ecs/Buildings.h"

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

// A spearman of ours at an enemy wall, struck by an enemy spearman: what it
// goes for after. `ordered`, a person sent it at the wall; `computer`, the
// computer plays it. The striker or the wall, or null.
entt::entity AtTheWall(bool ordered, bool computer) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[kUs].computer = computer;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    int x = grid.GetWidth() / 2, y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 4)) return entt::null;
    // No wolf or boar about to pick a fight of its own.
    std::vector<entt::entity> wild;
    for (auto e : reg.view<ECS::Components::Combat, ECS::Components::Owner>()) {
        if (reg.get<ECS::Components::Owner>(e).player_id == Core::PLAYER_GAIA) wild.push_back(e);
    }
    for (entt::entity e : wild) ECS::Mortality::Kill(reg, grid, e);
    const entt::entity wall = ECS::Buildings::Place(reg, grid, cfg.GetUnitIdByName("WALL"), kThem, x, y, true);
    const entt::entity ours = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("SPRMN"), kUs, At(x, y + 1), {}, 0);
    if (wall == entt::null || ours == entt::null) return entt::null;
    if (ordered) {
        ECS::Orders::Execute(reg, grid, Core::AttackCommand{ours, wall, kUs});
    } else {
        ECS::Combat::Engage(reg, ours, wall, false);
    }
    for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
    const entt::entity striker = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("SPRMN"), kThem, At(x + 2, y + 2), {}, 0);
    if (striker == entt::null) return entt::null;
    if (auto* combat = reg.try_get<ECS::Components::Combat>(striker)) combat->stance = ECS::Components::Stance::NoAttack;
    ECS::Combat::Hurt(reg, grid, ours, Core::Fixed32(1), striker, kThem);
    sim.Update(16);
    const auto& combat = reg.get<ECS::Components::Combat>(ours);
    return combat.target == striker ? striker : combat.target == wall ? wall : entt::null;
}

// A computer player's (`dodge`, `keep` its difficulty parameters) forager at
// work, set on by an enemy archer from afar: whether it ran before any arrow
// came. And its archer, shooting at an enemy villager, set on by an enemy
// spearman close by: whether it backed off. -1 for no field.
int SetOn(bool archer_case, int dodge, int keep) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[kUs].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    // The computer's rules doing nothing, and every computer and wild animal
    // left alone.
    if (AI::Brains* brains = reg.ctx().find<AI::Brains>()) brains->brains.clear();
    int x = grid.GetWidth() / 2, y = grid.GetHeight() / 2;
    if (!Clearing(grid, x, y, 8)) return -1;
    std::vector<entt::entity> wild;
    for (auto e : reg.view<ECS::Components::Combat, ECS::Components::Owner>()) {
        if (reg.get<ECS::Components::Owner>(e).player_id == Core::PLAYER_GAIA) wild.push_back(e);
    }
    for (entt::entity e : wild) ECS::Mortality::Kill(reg, grid, e);
    ECS::Orders::Execute(reg, grid, Core::DifficultyCommand{0, static_cast<uint8_t>(dodge), kUs});
    ECS::Orders::Execute(reg, grid, Core::DifficultyCommand{1, static_cast<uint8_t>(keep), kUs});
    auto place = [&](const char* name, Core::PlayerId owner, int dx, int dy) {
        return ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(name), owner, At(x + dx, y + dy), {}, 0);
    };
    if (!archer_case) {
        const entt::entity bush = place("FORAG", Core::PLAYER_GAIA, 0, 0);
        const entt::entity forager = place("VMBAS", kUs, 1, 0);
        if (bush == entt::null || forager == entt::null) return -1;
        ECS::Orders::Execute(reg, grid, Core::GatherCommand{forager, bush, kUs});
        for (int t = 0; t < 4 * kTicksPerSecond; ++t) sim.Update(16);
        const entt::entity archer = place("ARCHR", kThem, 1, 7);
        if (archer == entt::null) return -1;
        ECS::Combat::Engage(reg, archer, forager, true);
        for (int t = 0; t < 3; ++t) sim.Update(16);
        return reg.get<ECS::Components::Worker>(forager).state == Core::UnitState::Gathering ? 0 : 1;
    }
    const entt::entity ours = place("ARCHR", kUs, 0, 0);
    const entt::entity mark = place("VMBAS", kThem, 4, 0);
    if (ours == entt::null || mark == entt::null) return -1;
    if (auto* m = reg.try_get<ECS::Components::Worker>(mark)) m->state = Core::UnitState::Idle;
    ECS::Combat::Engage(reg, ours, mark, true);
    for (int t = 0; t < 4; ++t) sim.Update(16);
    const entt::entity spear = place("SPRMN", kThem, -1, 1);
    if (spear == entt::null) return -1;
    ECS::Combat::Engage(reg, spear, ours, true);
    for (int t = 0; t < 3; ++t) sim.Update(16);
    const auto* path = reg.try_get<ECS::Components::Path>(ours);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size() ? 1 : 0;
}

} // namespace

// What a villager at work does when struck. By a wolf, it breaks off, fights
// it, and goes back to its bush after; an idle villager and an idle spearman
// beside it come to help. By another player's soldier, it runs a few steps
// off and goes back, fighting nothing; the spearman comes, the villager does
// not. And a soldier at a wall, struck, turns on the striker unless a person
// sent it at the wall.
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
    // A soldier at a wall, struck: one that took the wall up itself turns on
    // the striker, one a person sent at it keeps on, and a computer player's
    // turns either way.
    if (verdict == nullptr) {
        const entt::entity unsent = AtTheWall(false, false);
        const entt::entity sent = AtTheWall(true, false);
        const entt::entity computers = AtTheWall(true, true);
        if (unsent == entt::null || sent == entt::null || computers == entt::null) {
            verdict = "no field for the wall";
        } else if (sent == unsent) {
            verdict = "a soldier at a wall it came upon did not turn on its striker, or one sent at it did";
        } else if (computers != unsent) {
            verdict = "a computer player's soldier sent at a wall did not turn on its striker";
        }
    }
    // A computer player's units set on: with ability-to-dodge-missiles 0 a
    // forager runs from an archer before any arrow lands, with 100 it keeps
    // at work; with ability-to-maintain-distance 0 an archer backs off from a
    // spearman setting on it, with 100 it stands.
    if (verdict == nullptr) {
        const int quick = SetOn(false, 0, 100), slow = SetOn(false, 100, 100);
        const int backs = SetOn(true, 100, 0), stands = SetOn(true, 100, 100);
        if (quick < 0 || slow < 0 || backs < 0 || stands < 0) verdict = "no field for the units set on";
        else if (quick != 1 || slow != 0) verdict = "a computer player's unit did not answer a ranged attacker as ability-to-dodge-missiles says";
        else if (backs != 1 || stands != 0) verdict = "a computer player's archer did not back off as ability-to-maintain-distance says";
    }
    std::printf("answer=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
