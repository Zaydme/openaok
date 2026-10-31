#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

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

} // namespace

// The population limits the game setup offers, past the original's 200 to
// 1000: the choices run from 25 up to 1000, the original's eight among them,
// and a game at the largest supports more than 200 where a game at 200 stops
// there.
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
    if (Core::kPopulationLimits[0] != 25 || most != 1000 || !rising) {
        verdict = "the choices do not run from 25 up to 1000";
    } else if (!original) {
        verdict = "an original choice is missing";
    } else if (at_200 != 200) {
        verdict = "a game at 200 did not stop at 200";
    } else if (at_most <= 200) {
        verdict = "a game at the largest limit stopped at 200";
    }
    std::printf("population=%s most=%d at_200=%d at_most=%d\n", verdict, most, at_200, at_most);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
