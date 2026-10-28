#include "verify/Harness.h"

#include <cstdlib>
#include <string>

#include "ecs/Digest.h"

namespace Verify {

uint64_t Fingerprint(const entt::registry& registry) {
    return ECS::Digest::Of(registry);
}

int Run(int ticks) {
    // Every scenario runs, even after one fails, so a single pass reports every
    // broken thing. Each builds its own world, so the order below carries no
    // meaning.
    //
    // AOE_VERIFY_ONLY=water,fishing runs just the scenarios named, by the word
    // their result line starts with; the gate never sets it.
    const char* only = std::getenv("AOE_VERIFY_ONLY");
    const std::string wanted = only != nullptr ? "," + std::string(only) + "," : "";
    auto runs = [&](const char* name) {
        return wanted.empty() || wanted.find("," + std::string(name) + ",") != std::string::npos;
    };
    bool ok = true;
    if (runs("entities")) ok &= CheckDeterminism(ticks);
    if (runs("gathering")) ok &= CheckGathering();
    if (runs("dropoff")) ok &= CheckDropoff();
    if (runs("sequence")) ok &= CheckSequence();
    if (runs("felling")) ok &= CheckFelling();
    if (runs("notices")) ok &= CheckNotices();
    if (runs("foundations")) ok &= CheckFoundations();
    if (runs("fog")) ok &= CheckFog();
    if (runs("flag")) ok &= CheckFlag();
    if (runs("spies")) ok &= CheckSpies();
    if (runs("capture")) ok &= CheckCapture();
    if (runs("planner")) ok &= CheckApproachPlanner();
    if (runs("building")) ok &= CheckConstruction();
    if (runs("production")) ok &= CheckProduction();
    if (runs("techs")) ok &= CheckTechs();
    if (runs("combat")) ok &= CheckCombat();
    if (runs("vision")) ok &= CheckVision();
    if (runs("garrison")) ok &= CheckGarrison();
    if (runs("animals")) ok &= CheckAnimals();
    if (runs("duties")) ok &= CheckDuties();
    if (runs("monks")) ok &= CheckMonks();
    if (runs("maps")) ok &= CheckMaps();
    if (runs("script")) ok &= CheckScript();
    if (runs("ai")) ok &= CheckAI();
    if (runs("pace")) ok &= CheckPace();
    if (runs("seven")) ok &= CheckSeven();
    if (runs("crossing")) ok &= CheckCrossing();
    if (runs("net")) ok &= CheckNet();
    if (runs("victory")) ok &= CheckVictory();
    if (runs("water")) ok &= CheckWater();
    if (runs("fishing")) ok &= CheckFishing();
    if (runs("navy")) ok &= CheckNavy();
    if (runs("landing")) ok &= CheckLanding();
    if (runs("market")) ok &= CheckMarket();
    if (runs("trade")) ok &= CheckTrade();
    if (runs("tribute")) ok &= CheckTribute();
    if (runs("diplomacy")) ok &= CheckDiplomacy();
    if (runs("chat")) ok &= CheckChat();
    if (runs("score")) ok &= CheckScore();
    if (runs("save")) ok &= CheckSave();
    if (runs("recording")) ok &= CheckRecording();
    if (runs("walls")) ok &= CheckWalls();
    if (runs("repair")) ok &= CheckRepair();
    if (runs("packing")) ok &= CheckPacking();
    if (runs("converted")) ok &= CheckConversion();
    if (runs("modes")) ok &= CheckModes();
    if (runs("cheats")) ok &= CheckCheats();
    if (runs("scenario")) ok &= CheckScenario();
    if (runs("waypoints")) ok &= CheckWaypoints();
    if (runs("editor")) ok &= CheckEditor();
    return ok ? 0 : 3;
}

} // namespace Verify
