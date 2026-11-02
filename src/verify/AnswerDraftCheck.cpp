#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ai/Brain.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "core/Command.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Combat.h"
#include "ecs/Orders.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;
constexpr Core::PlayerId kComputer = 1;
constexpr Core::PlayerId kEnemy = 2;

// The middle of the walkable tile nearest (x, y), searched ring by ring.
bool OpenSpot(const Physics::Grid& grid, int x, int y, ECS::Components::Vector2D& out) {
    for (int r = 0; r < 6; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r || !grid.IsWalkable(x + dx, y + dy)) continue;
                const Core::Fixed32 half = Core::Fixed32(1) / Core::Fixed32(2);
                out = {Core::Fixed32(x + dx) + half, Core::Fixed32(y + dy) + half};
                return true;
            }
        }
    }
    return false;
}

} // namespace

// A Hardest computer player whose rules do nothing, with four spearmen in
// its town: one idle, one fighting an enemy of its own accord, one its
// player sent at that enemy, one its player sent out of town. A villager of
// its struck in the town: the idle one and the one fighting of its own
// accord go for the striker; the other two keep to their orders.
bool CheckAnswerDraft() {
    const char* verdict = nullptr;
    Core::GameSetup setup = Core::DefaultSetup();
    setup.difficulty = 0;
    setup.players[kComputer].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) verdict = "no brain for the computer seat";

    entt::entity idle = entt::null, self = entt::null, ordered = entt::null, walking = entt::null;
    entt::entity foe = entt::null, raider = entt::null;
    if (verdict == nullptr) {
        AI::Brain& brain = brains->brains.front();
        AI::Script::Environment env;
        env.read = [](const std::string&, std::string& text) {
            text = "(defrule (true) => (do-nothing))\n";
            return true;
        };
        env.percent = [] { return 0; };
        brain.program = AI::Script::Compile("main", env);
        AI::Reset(brain.mind, brain.program.rules.size(), &setup);
        brain.mind.stance[kEnemy] = static_cast<uint8_t>(AI::Lang::Stance::Enemy);
        for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);

        const auto [hx, hy] = sim.player_starts[0];
        const int sx = grid.GetWidth() / 2 > hx ? 1 : -1, sy = grid.GetHeight() / 2 > hy ? 1 : -1;
        const int spear = cfg.GetUnitIdByName("SPRMN");
        const auto place = [&](int id, Core::PlayerId p, int dx, int dy) {
            ECS::Components::Vector2D at;
            if (!OpenSpot(grid, hx + sx * dx, hy + sy * dy, at)) return entt::entity{entt::null};
            return ECS::ScenarioWorld::Place(reg, grid, id, p, at, {}, 0);
        };
        // The fight well away from the blow, so neither side's soldiers see
        // the other's enemy.
        self = place(spear, kComputer, 12, 0);
        foe = place(spear, kEnemy, 13, 0);
        ordered = place(spear, kComputer, 12, 2);
        const entt::entity villager = place(cfg.GetUnitIdByName("VMBAS"), kComputer, 0, 9);
        raider = place(spear, kEnemy, 0, 10);
        idle = place(spear, kComputer, 3, 12);
        walking = place(spear, kComputer, 6, 7);
        if (idle == entt::null || self == entt::null || foe == entt::null || ordered == entt::null ||
            walking == entt::null || villager == entt::null || raider == entt::null) {
            verdict = "no room in the town";
        } else {
            // The enemy by `self` holds its fire, so the fight it is drawn
            // into is no blow for its player to answer.
            reg.get<ECS::Components::Combat>(foe).stance = ECS::Components::Stance::NoAttack;
            ECS::Orders::Execute(reg, grid, Core::AttackCommand{ordered, foe, kComputer});
            const Core::Fixed32 far_x(hx + sx * 40), far_y(hy + sy * 40);
            ECS::Orders::Execute(reg, grid, Core::MoveCommand{walking, far_x, far_y, kComputer});
            for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
            const auto* fight = reg.try_get<ECS::Components::Combat>(self);
            if (fight == nullptr || fight->target != foe || fight->ordered) {
                verdict = "the soldier by the enemy did not take it on of its own accord";
            } else {
                ECS::Combat::Hurt(reg, grid, villager, Core::Fixed32(1), raider, kEnemy);
                for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
            }
        }
    }

    const auto target = [&](entt::entity e) {
        const auto* combat = reg.try_get<ECS::Components::Combat>(e);
        return combat != nullptr ? combat->target : entt::entity{entt::null};
    };
    if (verdict == nullptr) {
        if (target(idle) != raider) verdict = "an idle soldier in the town did not go for the striker";
        else if (target(self) != raider) verdict = "a soldier fighting of its own accord did not go for the striker";
        else if (target(ordered) != foe) verdict = "a soldier its player sent to attack was taken off its target";
        else if (target(walking) == raider) verdict = "a soldier on its way out of town was turned back";
    }
    std::printf("answerdraft=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
