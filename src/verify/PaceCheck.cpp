#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/GameSetup.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "verify/AIGame.h"

namespace Verify {

namespace {

// When each difficulty's computer player reaches the Feudal Age, on the
// default map against a human seat that does nothing.
//
// Hard and moderate advance by the market rules (loaded from moderate up):
// the age is researched once its price is in hand with escrow, after 20
// villagers -- and from minute 10, when the town starts saving, half of what
// comes in is set aside for it. Easy and easiest advance by the "crude age
// advancement rules" of their difficulty files: only after a human player has
// reached that age, and as soon as they can after.
constexpr int kHardFeudalBy = 22;
constexpr int kModerateFeudalBy = 22;
// Easy and easiest: the computer player is handed the age's price at the
// first minute below, so it could advance at once -- and must not have begun
// to when the human seat goes to the Feudal Age at the second; then it
// follows at once, its two minutes' research done by the third.
constexpr int kPriceAt = 4;
constexpr int kHumanAdvancesAt = 6;
constexpr int kFollowedBy = 9;

// Whether `player` has the Feudal Age, or is researching it.
bool Advancing(const entt::registry& reg, Core::PlayerId player) {
    const ECS::Players::Player* seat = ECS::Players::Find(reg, player);
    const int tech = ECS::Techs::AgeTech(reg, player, 1);
    if (seat == nullptr || tech < 0) return false;
    const auto state = seat->techs[static_cast<size_t>(tech)].state;
    return state == ECS::Players::TechState::Researching || state == ECS::Players::TechState::Done;
}

const char* Paced(int difficulty, int& feudal) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].computer = false;
    setup.players[2].computer = true;
    setup.difficulty = difficulty;
    // For tuning: another map seed (the gate never sets it).
    if (const char* seed = std::getenv("AOE_AI_SEED")) setup.seed = static_cast<uint32_t>(std::atoi(seed));
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    const bool trace = std::getenv("AOE_AI_TRACE") != nullptr;
    const bool follows = difficulty >= 3;
    const int limit = follows ? kFollowedBy : (difficulty == 1 ? kHardFeudalBy : kModerateFeudalBy);
    AIGame::Progress progress;
    for (int minute = 1; minute <= limit && progress.feudal_minute[2] < 0; ++minute) {
        if (follows && minute == kPriceAt + 1) {
            if (ECS::Players::Player* seat = ECS::Players::Find(reg, 2)) {
                ECS::Players::AddStock(*seat, Core::ResourceType::Food, 1000);
            }
        }
        if (follows && minute == kHumanAdvancesAt + 1) {
            if (Advancing(reg, 2)) return "an easy computer player advanced before the human";
            ECS::Techs::Complete(reg, 1, ECS::Techs::AgeTech(reg, 1, 1));
        }
        AIGame::Minute(sim, nullptr, minute, progress);
        if (trace) AIGame::Trace(reg, minute, 2);
    }
    feudal = progress.feudal_minute[2];
    if (follows && feudal >= 0 && feudal <= kHumanAdvancesAt) return "an easy computer player advanced before the human";
    if (feudal < 0) {
        return follows ? "an easy computer player did not follow the human into the Feudal Age"
                       : "a hard or moderate computer player reached the Feudal Age late";
    }
    return nullptr;
}

} // namespace

bool CheckPace() {
    const char* verdict = nullptr;
    int feudal[5] = {-1, -1, -1, -1, -1};
    for (int d = 1; d <= 4 && verdict == nullptr; ++d) verdict = Paced(d, feudal[d]);
    if (std::getenv("AOE_AI_TRACE") != nullptr) {
        std::printf("AI pace: feudal hard %d moderate %d easy %d easiest %d\n", feudal[1], feudal[2],
                    feudal[3], feudal[4]);
    }
    std::printf("pace=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
