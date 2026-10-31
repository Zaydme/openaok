#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "ai/Brain.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "ecs/Combat.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
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

    // A Hardest computer player whose rules are `rules` (by default doing
    // nothing), with spearmen standing together by its Town Center; `known`,
    // the enemy's town explored.
    const char* Open(bool known, const char* rules = "(defrule (true) => (do-nothing))\n") {
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
        const std::string script = rules;
        env.read = [&](const std::string&, std::string& text) {
            text = script;
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

// Out of town (sn-maximum-town-size and the sentry distance) but within the
// enemy-sighted response distance of the soldiers at home.
constexpr int kOutOfTown = 35;

// A blow at a thing of the computer's out of town, by an enemy spearman:
// how many of the spearmen at home go for the striker. `scout` strikes the
// exploring scout instead of a villager.
const char* Answered(bool scout, int& answered) {
    Town town;
    if (const char* v = town.Open(false)) return v;
    auto& reg = town.sim.GetRegistry();
    auto& grid = town.sim.GetGrid();
    AI::SetStrategic(town.brain->mind, SN::NumberAttackGroups, 0, &town.setup);
    AI::SetStrategic(town.brain->mind, SN::PercentEnemySightedResponse, 60, &town.setup);
    AI::SetStrategic(town.brain->mind, SN::NumberExploreGroups, 1, &town.setup);
    town.Run(2);
    const auto [hx, hy] = town.sim.player_starts[0];
    auto& cfg = Core::ConfigManager::Get();
    const auto at = [&](int x, int y) {
        return ECS::Components::Vector2D{Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2),
                                         Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)};
    };
    // Open ground for the two, about kOutOfTown tiles off, towards the map's
    // middle.
    const int mx = grid.GetWidth() / 2, my = grid.GetHeight() / 2;
    const int sx = mx > hx ? 1 : -1, sy = my > hy ? 1 : -1;
    int vx = -1, vy = -1;
    for (int r = 0; r < 8 && vx < 0; ++r) {
        for (int d = -r; d <= r && vx < 0; ++d) {
            const int x = hx + sx * (kOutOfTown * 7 / 10) + d, y = hy + sy * (kOutOfTown * 7 / 10) + r;
            bool open = true;
            for (int oy = 0; oy <= 2 && open; ++oy) open = grid.IsWalkable(x, y + oy) && grid.IsWalkable(x + 1, y + oy);
            if (open) {
                vx = x;
                vy = y;
            }
        }
    }
    if (vx < 0) return "no open ground out of town";
    entt::entity victim = entt::null;
    if (scout) {
        for (auto e : reg.view<ECS::Components::Identity, ECS::Components::Owner>()) {
            if (reg.get<ECS::Components::Owner>(e).player_id == kComputer &&
                reg.get<ECS::Components::Identity>(e).unit_id == cfg.GetUnitIdByName("SCOUT")) {
                victim = e;
            }
        }
        // Struck where the villager would be, out of town, beyond any idle
        // soldier's help.
        if (victim != entt::null) {
            auto& t = reg.get<ECS::Components::Transform>(victim);
            t.position = t.previous_position = at(vx, vy);
            reg.remove<ECS::Components::Path>(victim);
        }
    } else {
        victim = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), kComputer, at(vx, vy), {}, 0);
    }
    const entt::entity striker = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("SPRMN"), kEnemy, at(vx, vy + 2), {}, 0);
    if (victim == entt::null || striker == entt::null) return "no room for the blow";
    ECS::Combat::Hurt(reg, grid, victim, Core::Fixed32(1), striker, kEnemy);
    town.Run(2);
    answered = 0;
    for (entt::entity e : town.spears) {
        const auto* combat = reg.try_get<ECS::Components::Combat>(e);
        answered += combat != nullptr && combat->target == striker ? 1 : 0;
    }
    return nullptr;
}

// Two computer players on one team, 1 and 3, and player 2 their enemy, the
// computers' rules doing nothing; spearmen by player 1's Town Center and two
// by player 3's.
struct Allies {
    ECS::Simulation sim;
    Core::GameSetup setup;
    AI::Brain* one = nullptr;
    AI::Brain* three = nullptr;
    std::vector<entt::entity> spears;

    const char* Open() {
        setup = Core::DefaultSetup();
        setup.difficulty = 0;
        setup.players[1].computer = true;
        setup.players[3].active = true;
        setup.players[3].computer = true;
        setup.players[1].team = setup.players[3].team = 1;
        setup.players[2].team = 2;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        AI::Brains* brains = reg.ctx().find<AI::Brains>();
        if (brains == nullptr || brains->brains.size() != 2) return "no brains for the two computers";
        for (AI::Brain& b : brains->brains) {
            AI::Script::Environment env;
            env.read = [](const std::string&, std::string& text) {
                text = "(defrule (true) => (do-nothing))\n";
                return true;
            };
            env.percent = [] { return 0; };
            b.program = AI::Script::Compile("main", env);
            AI::Reset(b.mind, b.program.rules.size(), &setup);
            AI::SetStrategic(b.mind, SN::GroupFormDistance, 10, &setup);
            AI::SetStrategic(b.mind, SN::CoopShareInformation, 0, &setup);
            (b.player == 1 ? one : three) = &b;
        }
        if (one == nullptr || three == nullptr || sim.player_starts.size() < 3) return "no third seat";
        if (!Place(1, sim.player_starts[0], kSpearmen) || !Place(3, sim.player_starts[2], 2)) return "no room for the spearmen";
        return nullptr;
    }

    bool Place(Core::PlayerId p, std::pair<int, int> start, int n) {
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        const int spearman = Core::ConfigManager::Get().GetUnitIdByName("SPRMN");
        int placed = 0;
        for (int r = 4; r < 14 && placed < n; ++r) {
            for (int dx = -r; dx <= r && placed < n; dx += 2) {
                const int x = start.first + dx, y = start.second + r;
                if (!grid.IsWalkable(x, y)) continue;
                const entt::entity e = ECS::ScenarioWorld::Place(
                    reg, grid, spearman, p,
                    {Core::Fixed32(x) + Core::Fixed32(1) / Core::Fixed32(2), Core::Fixed32(y) + Core::Fixed32(1) / Core::Fixed32(2)},
                    {}, 0);
                if (e == entt::null) continue;
                ++placed;
                if (p == 1) spears.push_back(e);
            }
        }
        return placed == n;
    }

    void Run(int seconds) {
        for (int t = 0; t < seconds * kTicksPerSecond; ++t) sim.Update(16);
    }

    int Attacking() {
        auto& reg = sim.GetRegistry();
        int n = 0;
        for (entt::entity e : spears) {
            const auto* combat = reg.valid(e) ? reg.try_get<ECS::Components::Combat>(e) : nullptr;
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
// before sn-initial-attack-delay. And blows answered as the game answers
// them: a share, rounded down, of the idle soldiers near the one struck, and
// none for an explorer. Allied computer players share what they find
// (sn-coop-share-information) and answer one another's calls for help
// (sn-coop-share-attacking).
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
    // attack-soldier-count counts the soldiers of every attack group, one
    // still waiting for an enemy to go for among them.
    if (verdict == nullptr) {
        Town town;
        verdict = town.Open(false, "(defrule (attack-soldier-count >= 4) => (set-goal 1 1) (disable-self))\n");
        if (verdict == nullptr) {
            town.Run(40);
            if (town.brain->mind.goals[1] != 1) verdict = "attack-soldier-count left out an attack group waiting at home";
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
    // Blows answered: 60% of the six idle spearmen, rounded down, go for an
    // enemy that struck a villager out of town; none for the exploring
    // scout.
    if (verdict == nullptr) {
        int answered = 0;
        verdict = Answered(false, answered);
        if (verdict == nullptr && answered != 3) verdict = "a blow out of town was not answered by the game's share of the idle soldiers";
    }
    if (verdict == nullptr) {
        int answered = 0;
        verdict = Answered(true, answered);
        if (verdict == nullptr && answered != 0) verdict = "a blow at the exploring scout was answered";
    }
    // Allies: what player 3 has explored of the enemy's town, player 1
    // knows when player 3 shares what it finds, and attacks.
    for (const int share : {1, 0}) {
        if (verdict != nullptr) break;
        Allies a;
        verdict = a.Open();
        if (verdict != nullptr) break;
        AI::SetStrategic(a.three->mind, SN::CoopShareInformation, share, &a.setup);
        const auto [ex, ey] = a.sim.player_starts[1];
        ECS::Vision::Explore(a.sim.GetRegistry(), 3, ex, ey, kKnownRadius);
        a.Run(60);
        if (share == 1 && a.Attacking() == 0) verdict = "a computer player did not know what its ally shared of the enemy";
        if (share == 0 && a.Attacking() != 0) verdict = "a computer player knew what its ally did not share";
    }
    // And a call for help: player 1 struck by player 2's, player 3 goes
    // for player 2 with the attack group it has, short as it is.
    if (verdict == nullptr) {
        Allies a;
        verdict = a.Open();
        if (verdict == nullptr) {
            a.Run(5);
            auto& reg = a.sim.GetRegistry();
            auto& grid = a.sim.GetGrid();
            const auto [hx, hy] = a.sim.player_starts[0];
            const entt::entity striker = ECS::ScenarioWorld::Place(
                reg, grid, Core::ConfigManager::Get().GetUnitIdByName("SPRMN"), kEnemy,
                {Core::Fixed32(hx + 3), Core::Fixed32(hy - 4)}, {}, 0);
            if (striker == entt::null) verdict = "no room for the striker";
            if (verdict == nullptr) {
                ECS::Combat::Hurt(reg, grid, a.spears.front(), Core::Fixed32(1), striker, kEnemy);
                a.Run(2);
                bool sent = false;
                for (const AI::Group& g : a.three->attack_groups) {
                    sent |= !g.units.empty() && g.wanted == static_cast<int>(g.units.size());
                }
                if (a.three->foe != kEnemy || !sent) verdict = "an allied computer player did not answer a call for help";
            }
        }
    }
    // sn-gather-defense-units: an idle spearman in no group, far from the
    // Town Center, is walked to it within the minute; without, it stays.
    for (const int gather : {1, 0}) {
        if (verdict != nullptr) break;
        Town town;
        verdict = town.Open(false);
        if (verdict != nullptr) break;
        AI::SetStrategic(town.brain->mind, SN::NumberAttackGroups, 0, &town.setup);
        AI::SetStrategic(town.brain->mind, SN::GatherDefenseUnits, gather, &town.setup);
        auto& reg = town.sim.GetRegistry();
        auto& grid = town.sim.GetGrid();
        const auto [hx, hy] = town.sim.player_starts[0];
        const int mx = grid.GetWidth() / 2, my = grid.GetHeight() / 2;
        int fx = hx + (mx > hx ? 15 : -15), fy = hy + (my > hy ? 15 : -15);
        while (!grid.IsWalkable(fx, fy) && fx != hx) fx += fx > hx ? -1 : 1;
        const entt::entity far = ECS::ScenarioWorld::Place(
            reg, grid, Core::ConfigManager::Get().GetUnitIdByName("SPRMN"), kComputer,
            {Core::Fixed32(fx) + Core::Fixed32(1) / Core::Fixed32(2), Core::Fixed32(fy) + Core::Fixed32(1) / Core::Fixed32(2)},
            {}, 0);
        if (far == entt::null) {
            verdict = "no room for the far spearman";
            break;
        }
        town.Run(75);
        const auto& at = reg.get<ECS::Components::Transform>(far).position;
        const bool home = Core::FixedMath::Distance(at.x - Core::Fixed32(hx), at.y - Core::Fixed32(hy)) <= Core::Fixed32(9);
        if (gather == 1 && !home) verdict = "an idle soldier in no group was not gathered at the Town Center";
        if (gather == 0 && home) verdict = "an idle soldier was gathered at the Town Center with sn-gather-defense-units 0";
    }
    std::printf("attackgroups=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
