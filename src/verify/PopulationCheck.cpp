#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "ai/Brain.h"
#include "ai/Script.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Population.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"

namespace Verify {

namespace {

constexpr int kHouses = 60;   // five each: room for 300 with the Town Center's five

// What player 1 can support in a game with `limit`, its Town Center and
// kHouses houses standing.
int CapacityWith(int limit) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.population_limit = limit;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const int house = Core::ConfigManager::Get().GetUnitIdByName("HOUS");
    const auto& start = sim.player_starts[0];
    for (int i = 0; i < kHouses; ++i) {
        const int x = start.first - 20 + (i % 10) * 4;
        const int y = start.second + 6 + (i / 10) * 4;
        ECS::ScenarioWorld::Place(reg, grid, house, 1, {Core::Fixed32(x), Core::Fixed32(y)}, {}, 0);
    }
    return ECS::Population::Capacity(reg, 1);
}

// The computer player's script for seat 2 at its hardest in a game with
// `limit`: how many errors it loaded with, and its pop-cap and unit-max.
struct Script {
    size_t errors = 0;
    int pop_cap = -1;
    int unit_max = -1;
};

Script ScriptWith(int limit) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.population_limit = limit;
    setup.difficulty = 0;
    setup.players[2].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    Script out;
    const AI::Brains* brains = sim.GetRegistry().ctx().find<AI::Brains>();
    if (brains == nullptr || brains->brains.empty()) return out;
    const AI::Script::Program& program = brains->brains.front().program;
    out.errors = program.errors.size();
    AI::Script::Constant(program, "pop-cap", out.pop_cap);
    AI::Script::Constant(program, "unit-max", out.unit_max);
    return out;
}

} // namespace

// The population limits the game setup offers, past the original's 200 to
// 1000: the choices run from 25 up to 1000, the original's eight among them,
// and a game at the largest supports more than 200 where a game at 200 stops
// there. The computer players' scripts, written for limits up to 200, load
// at the largest without an error, their pop-cap the limit and unit-max
// scaled from 200's.
bool CheckPopulation() {
    const char* verdict = "ok";
    bool rising = true;
    for (int i = 1; i < Core::kPopulationChoices; ++i) {
        rising = rising && Core::kPopulationLimits[i] > Core::kPopulationLimits[i - 1];
    }
    bool original = true;
    for (int n = 25; n <= 200; n += 25) {
        bool found = false;
        for (int limit : Core::kPopulationLimits) found = found || limit == n;
        original = original && found;
    }
    const int most = Core::kPopulationLimits[Core::kPopulationChoices - 1];
    const int at_200 = CapacityWith(200);
    const int at_most = CapacityWith(most);
    const Script script_200 = ScriptWith(200);
    const Script script_most = ScriptWith(most);
    if (Core::kPopulationLimits[0] != 25 || most != 1000 || !rising) {
        verdict = "the choices do not run from 25 up to 1000";
    } else if (!original) {
        verdict = "an original choice is missing";
    } else if (at_200 != 200) {
        verdict = "a game at 200 did not stop at 200";
    } else if (at_most <= 200) {
        verdict = "a game at the largest limit stopped at 200";
    } else if (script_200.errors != 0 || script_200.pop_cap != 200) {
        verdict = "the computer players' script did not load at 200";
    } else if (script_most.errors != 0) {
        verdict = "the computer players' script lost rules at the largest limit";
    } else if (script_most.pop_cap != most || script_most.unit_max != script_200.unit_max * most / 200) {
        verdict = "the computer players' script kept 200's figures at the largest limit";
    }
    std::printf("population=%s most=%d at_200=%d at_most=%d script_errors=%zu pop_cap=%d unit_max=%d\n", verdict,
                most, at_200, at_most, script_most.errors, script_most.pop_cap, script_most.unit_max);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
