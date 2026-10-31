#include "verify/Harness.h"

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
#include "ecs/components/Health.h"

namespace Verify {

namespace {

using SN = AI::Lang::SN;
constexpr int kTicksPerSecond = 1000 / 16;
constexpr Core::PlayerId kComputer = 1;
constexpr Core::PlayerId kEnemy = 2;
constexpr int kSpearmen = 6;
// How long the attack group has to set out, in seconds.
constexpr int kSetOutS = 60;
// The open ground each enemy building is put on, and how far round it the
// computer has explored.
constexpr int kRoom = 5;
constexpr int kExplored = 4;

Core::Fixed32 Half() { return Core::Fixed32(1) / Core::Fixed32(2); }

// One enemy building to be put out of the computer's town: what, how many
// tiles from home, and the share of its hit points left (percent).
struct Piece {
    const char* name;
    int distance;
    int percent = 100;
};

// A Hardest computer player whose rules do nothing, its spearmen by its Town
// Center, the enemy's `pieces` out of town towards the map's middle -- each
// explored -- and `special` its sn-special-attack-type1. What its first attack
// group goes for: the index of the piece, -1 for none, -2 for something else.
const char* Attack(const std::vector<Piece>& pieces, int special, int& chosen) {
    ECS::Simulation sim;
    Core::GameSetup setup = Core::DefaultSetup();
    setup.difficulty = 0;
    setup.players[kComputer].computer = true;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const auto& cfg = Core::ConfigManager::Get();
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) return "no brain for the computer seat";
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
    AI::SetStrategic(brain.mind, SN::GroupFormDistance, 10, &setup);
    AI::SetStrategic(brain.mind, SN::SpecialAttackType1, special, &setup);

    const auto [hx, hy] = sim.player_starts[0];
    const int spearman = cfg.GetUnitIdByName("SPRMN");
    int placed = 0;
    for (int r = 4; r < 14 && placed < kSpearmen; ++r) {
        for (int dx = -r; dx <= r && placed < kSpearmen; dx += 2) {
            const int x = hx + dx, y = hy + r;
            if (!grid.IsWalkable(x, y)) continue;
            placed += ECS::ScenarioWorld::Place(reg, grid, spearman, kComputer,
                                                {Core::Fixed32(x) + Half(), Core::Fixed32(y) + Half()}, {}, 0) !=
                              entt::null
                          ? 1
                          : 0;
        }
    }
    if (placed != kSpearmen) return "no room for the spearmen";

    // Along the line from home to the map's middle, each on open ground
    // nearest its distance, side by side.
    const Core::Fixed32 cx(grid.GetWidth() / 2), cy(grid.GetHeight() / 2);
    const Core::Fixed32 dx = cx - Core::Fixed32(hx), dy = cy - Core::Fixed32(hy);
    const Core::Fixed32 length = Core::FixedMath::Distance(dx, dy);
    std::vector<entt::entity> placed_pieces;
    for (const Piece& piece : pieces) {
        const int ix = (Core::Fixed32(hx) + dx * Core::Fixed32(piece.distance) / length).ToInt();
        const int iy = (Core::Fixed32(hy) + dy * Core::Fixed32(piece.distance) / length).ToInt();
        entt::entity e = entt::null;
        for (int ring = 0; ring < 8 && e == entt::null; ++ring) {
            for (int oy = -ring; oy <= ring && e == entt::null; ++oy) {
                for (int ox = -ring; ox <= ring && e == entt::null; ++ox) {
                    bool open = true;
                    for (int ty = -kRoom / 2; ty <= kRoom / 2 && open; ++ty) {
                        for (int tx = -kRoom / 2; tx <= kRoom / 2 && open; ++tx) {
                            open = grid.IsWalkable(ix + ox + tx, iy + oy + ty);
                        }
                    }
                    if (!open) continue;
                    e = ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName(piece.name), kEnemy,
                                                  {Core::Fixed32(ix + ox) + Half(), Core::Fixed32(iy + oy) + Half()},
                                                  {}, 0);
                    if (e != entt::null) ECS::Vision::Explore(reg, kComputer, ix + ox, iy + oy, kExplored);
                }
            }
        }
        if (e == entt::null) return "no room for an enemy building";
        auto& health = reg.get<ECS::Components::Health>(e);
        health.current_hp = health.max_hp * piece.percent / 100;
        placed_pieces.push_back(e);
    }

    chosen = -1;
    for (int s = 0; s < kSetOutS && chosen == -1; ++s) {
        for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
        for (const AI::Group& g : brain.attack_groups) {
            if (g.target == entt::null) continue;
            chosen = -2;
            for (size_t i = 0; i < placed_pieces.size(); ++i) {
                if (placed_pieces[i] == g.target) chosen = static_cast<int>(i);
            }
            break;
        }
    }
    return nullptr;
}

} // namespace

bool CheckTargets() {
    struct Case {
        std::vector<Piece> pieces;
        int special;
        int expected;
        const char* wrong;
    };
    // Scores out of 100 (see ai/Military): a damaged barracks and a house
    // each best by one measure tie at 75, and the barracks goes first; a wall
    // is taken after a house; of three barracks the damaged middle one scores
    // 92 to the near one's 86 and the far one's 70, and the Monastery behind
    // them 50, or 100 with sn-special-attack-type1.
    const Case cases[] = {
        {{{"HOUS", 28}, {"BRKS", 34, 10}}, 0, 1, "a house was attacked before a barracks"},
        {{{"WALL", 28}, {"HOUS", 34}}, 0, 1, "a wall was attacked before a house"},
        {{{"BRKS", 28}, {"BRKS", 32, 10}, {"BRKS", 36}, {"CRCH", 40}}, 0, 1,
         "the best scored barracks was not attacked first"},
        {{{"BRKS", 28}, {"BRKS", 32, 10}, {"BRKS", 36}, {"CRCH", 40}}, 1, 3,
         "the Monastery was not attacked first with sn-special-attack-type1"},
    };
    const char* verdict = nullptr;
    for (const Case& c : cases) {
        int chosen = -1;
        verdict = Attack(c.pieces, c.special, chosen);
        if (verdict == nullptr && chosen == -1) verdict = "the attack group did not set out";
        if (verdict == nullptr && chosen != c.expected) verdict = c.wrong;
        if (verdict != nullptr) break;
    }
    std::printf("targets=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
