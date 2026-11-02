#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ai/Brain.h"
#include "ai/Language.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Combat.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;

// A villager of the computer player's (seat 2) `out` tiles past its town's
// size from its Town Center, struck once by a spearman of seat 1's: whether
// the computer player takes it as a blow in its town.
bool StruckInTown(int out) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[2].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    AI::Brain* brain = nullptr;
    if (brains != nullptr) {
        for (AI::Brain& b : brains->brains) {
            if (b.player == 2) brain = &b;
        }
    }
    entt::entity tc = entt::null;
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == cfg.GetUnitIdByName("RTWC") &&
            view.get<const ECS::Components::Owner>(e).player_id == 2) {
            tc = e;
        }
    }
    if (brain == nullptr || tc == entt::null) return false;
    const int town = std::max(8, brain->mind.sn[static_cast<size_t>(AI::Lang::SN::MaximumTownSize)]);
    const auto& home = reg.get<ECS::Components::Transform>(tc).position;
    // Toward the map's middle, so the spot is on the map.
    const int dir_x = home.x.ToInt() < grid.GetWidth() / 2 ? 1 : -1;
    const ECS::Components::Vector2D at{home.x + Core::Fixed32(dir_x * (town + out)), home.y};
    const entt::entity villager =
        ECS::ScenarioWorld::Place(reg, grid, cfg.GetUnitIdByName("VMBAS"), 2, at, {}, 0);
    const entt::entity spearman = ECS::ScenarioWorld::Place(
        reg, grid, cfg.GetUnitIdByName("SPRMN"), 1, {at.x + Core::Fixed32(1), at.y}, {}, 0);
    if (villager == entt::null || spearman == entt::null) return false;
    brain->town_struck_ms = -1;
    ECS::Combat::Hurt(reg, grid, villager, Core::Fixed32(1), spearman);
    for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
    return brain->town_struck_ms >= 0;
}

} // namespace

// A computer player takes a blow to its own as struck in its town within its
// town's size and four tiles more of its Town Center, and not further out.
bool CheckTownStruck() {
    const char* verdict = "ok";
    const bool near = StruckInTown(2);
    const bool far = StruckInTown(8);
    if (!near) {
        verdict = "a blow two tiles past the town's size was not taken as in the town";
    } else if (far) {
        verdict = "a blow eight tiles past the town's size was taken as in the town";
    }
    std::printf("townstruck=%s near=%d far=%d\n", verdict, near ? 1 : 0, far ? 1 : 0);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
