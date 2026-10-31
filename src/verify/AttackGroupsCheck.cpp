#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "ai/Brain.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

using SN = AI::Lang::SN;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr Core::PlayerId kComputer = 1;
constexpr Core::PlayerId kEnemy = 2;
// The spearmen at home, and how far round the enemy's start the computer
// has explored when it knows where the enemy is.
constexpr int kSpearmen = 6;
constexpr int kKnownRadius = 10;
// Further than this from home is out of town.
constexpr int kHome = 20;

struct Town {
    ECS::Simulation sim;
    Core::GameSetup setup;
    AI::Brain* brain = nullptr;
    std::vector<entt::entity> spears;

    // A Hardest computer player whose rules do nothing, with spearmen
    // standing together by its Town Center; `known`, the enemy's town
    // explored.
    const char* Open(bool known) {
        setup = Core::DefaultSetup();
        setup.difficulty = 0;
        setup.players[kComputer].computer = true;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        AI::Brains* brains = reg.ctx().find<AI::Brains>();
        if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
        brain = &brains->brains.front();
        AI::Script::Environment env;
        env.read = [](const std::string&, std::string& text) {
            text = "(defrule (true) => (do-nothing))\n";
            return true;
        };
        env.percent = [] { return 0; };
        brain->program = AI::Script::Compile("main", env);
        AI::Reset(brain->mind, brain->program.rules.size(), &setup);
        brain->mind.stance[kEnemy] = static_cast<uint8_t>(AI::Lang::Stance::Enemy);
        AI::SetStrategic(brain->mind, SN::GroupFormDistance, 10, &setup);
        const auto [hx, hy] = sim.player_starts[0];
        const int spearman = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
        for (int r = 4; r < 14 && static_cast<int>(spears.size()) < kSpearmen; ++r) {
            for (int dx = -r; dx <= r && static_cast<int>(spears.size()) < kSpearmen; dx += 2) {
                const int x = hx + dx, y = hy + r;
                if (!grid.IsWalkable(x, y)) continue;
                const entt::entity e = ECS::ScenarioWorld::Place(
                    reg, grid, spearman, kComputer,
                    {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2), Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)},
                    {}, 0);
                if (e != entt::null) spears.push_back(e);
            }
        }
        if (static_cast<int>(spears.size()) != kSpearmen) return "no room for the spearmen";
        if (known) {
            const auto [ex, ey] = sim.player_starts[1];
            ECS::Vision::Explore(reg, kComputer, ex, ey, kKnownRadius);
        }
        return nullptr;
    }

    void Run(int seconds) {
        for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
    }

    // The spearmen more than kHome tiles from the Town Center's start.
    int Strayed() {
        auto& reg = sim.GetRegistry();
        const auto [hx, hy] = sim.player_starts[0];
        int n = 0;
        for (entt::entity e : spears) {
            if (!reg.valid(e) || !reg.all_of<ECS::Components::Transform>(e)) continue;
            const auto& at = reg.get<ECS::Components::Transform>(e).position;
            const Core::Fixed32 dx = at.x - Core::Fixed32(hx), dy = at.y - Core::Fixed32(hy);
            n += Core::FixedMath::Distance(dx, dy) > Core::Fixed32(kHome) ? 1 : 0;
        }
        return n;
    }

    // The spearmen fighting or on their way to the enemy: struck at a thing
    // of its, or with one for a target.
    int Attacking() {
        auto& reg = sim.GetRegistry();
        int n = 0;
        for (entt::entity e : spears) {
            if (!reg.valid(e)) continue;
            const auto* combat = reg.try_get<ECS::Components::Combat>(e);
            if (combat == nullptr || !reg.valid(combat->target)) continue;
            const auto* owner = reg.try_get<ECS::Components::Owner>(combat->target);
            n += owner != nullptr && owner->player_id == kEnemy ? 1 : 0;
        }
        return n;
    }
};

} // namespace

// A computer player's attack groups, as the game runs them: with the
// enemy's town known and no attack-now, the six spearmen at home make an
// attack group of four or five (sn-minimum-attack-group-size 4, a random one
// more) which gathers and attacks, while the rest wait for a group of their
// own; with the enemy unknown none goes, nor wanders off to look; with sn-number-attack-groups 0
// there are no groups; and with sn-initial-attack-delay-type 2 none goes
// before sn-initial-attack-delay.
bool CheckAttackGroups() {
    const char* verdict = nullptr;
    {
        Town town;
        verdict = town.Open(true);
        if (verdict == nullptr) {
            town.Run(60);
            const int out = town.Attacking();
            int largest = 0;
            for (const AI::Group& g : town.brain->attack_groups) {
                if (g.target != entt::null) largest = std::max(largest, static_cast<int>(g.units.size()));
            }
            if (out == 0) verdict = "a full, gathered attack group did not attack the enemy it knew";
            else if (largest < 4 || largest > 5) verdict = "the attack group was not the game's size";
            else if (out > largest) verdict = "soldiers outside the attack group went to attack";
        }
    }
    if (verdict == nullptr) {
        Town town;
        verdict = town.Open(false);
        if (verdict == nullptr) {
            town.Run(40);
            if (town.Attacking() != 0 || town.Strayed() > 0) {
                verdict = "an attack group set out against an enemy the player had not found";
            }
        }
    }
    if (verdict == nullptr) {
        Town town;
        verdict = town.Open(true);
        if (verdict == nullptr) {
            AI::SetStrategic(town.brain->mind, SN::NumberAttackGroups, 0, &town.setup);
            town.Run(40);
            if (!town.brain->attack_groups.empty() || town.Attacking() != 0) {
                verdict = "attack groups were made with sn-number-attack-groups 0";
            }
        }
    }
    if (verdict == nullptr) {
        Town town;
        verdict = town.Open(true);
        if (verdict == nullptr) {
            AI::SetStrategic(town.brain->mind, SN::InitialAttackDelayType, 2, &town.setup);
            AI::SetStrategic(town.brain->mind, SN::InitialAttackDelay, 90, &town.setup);
            town.Run(80);
            if (town.Attacking() != 0) verdict = "an attack group set out before the initial attack delay was over";
            town.Run(40);
            if (verdict == nullptr && town.Attacking() == 0) verdict = "no attack group set out after the initial attack delay";
        }
    }
    std::printf("attackgroups=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
