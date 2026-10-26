#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "ai/Brain.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Events.h"
#include "ecs/Navigation.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "pathfinding/Pathfinder.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

constexpr Core::PlayerId kComputer = 1;
constexpr Core::PlayerId kHuman = 2;

entt::entity FirstOf(const entt::registry& reg, int unit_id, Core::PlayerId player) {
    auto all = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : all) {
        if (all.get<const ECS::Components::Identity>(e).unit_id == unit_id &&
            all.get<const ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

} // namespace

// An attack across the water by the tactical AI: on Islands, a computer
// player whose rules say attack-now, with knights at home, a transport at its
// Dock and nothing of the enemy's to walk to. The knights have to board, be
// sailed over, step ashore on the enemy's island and strike there.
bool CheckCrossing() {
    const char* verdict = nullptr;
    auto& cfg = Core::ConfigManager::Get();
    const int dock_id = cfg.GetUnitIdByName("DOCK");
    const int transport_id = cfg.GetUnitIdByName("XPORT");
    const int knight_id = cfg.GetUnitIdByName("KNGHT");
    const int tc_id = cfg.GetUnitIdByName("RTWC");

    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Islands";
    setup.starting_age = 2;
    setup.players[kComputer].computer = true;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const auto [ax, ay] = sim.player_starts[0];
    const auto [dx, dy] = sim.player_starts[1];
    const entt::entity home = FirstOf(reg, tc_id, kComputer);
    const int their_island = Pathfinding::RegionAt(grid, dx + 3, dy + 3);
    const int our_island = Pathfinding::RegionAt(grid, ax + 3, ay + 3);
    if (home == entt::null || their_island < 0 || their_island == our_island) {
        verdict = "Islands has no two islands to try";
    }

    // The fleet and the army: a Dock and a transport, four knights.
    entt::entity ship = entt::null;
    if (verdict == nullptr) {
        const entt::entity dock = Shore::RaiseNear(sim, kComputer, dock_id, ax, ay, 40);
        if (dock == entt::null) verdict = "no shore on the first island takes a dock";
        ECS::Components::Vector2D at{};
        if (verdict == nullptr && ECS::Buildings::Doorstep(reg, grid, dock, at)) {
            ship = ECS::Units::Spawn(reg, transport_id, kComputer, at);
        }
        if (verdict == nullptr && ship == entt::null) verdict = "no transport launched";
    }
    std::vector<entt::entity> knights;
    if (verdict == nullptr) {
        for (int i = 0; i < 4; ++i) {
            ECS::Components::Vector2D at{};
            if (!ECS::Buildings::Doorstep(reg, grid, home, at)) break;
            knights.push_back(ECS::Units::Spawn(reg, knight_id, kComputer, at));
        }
        if (knights.size() != 4) verdict = "no room by the Town Center for the knights";
    }
    // Its rules: attack now, with every soldier.
    AI::Brains* brains = reg.ctx().find<AI::Brains>();
    if (verdict == nullptr && (brains == nullptr || brains->brains.empty())) verdict = "no brain for the computer";
    if (verdict == nullptr) {
        AI::Brain& brain = brains->brains.front();
        AI::Script::Environment env;
        const std::string text =
            "(defrule (true) => (set-strategic-number sn-percent-attack-soldiers 100) (attack-now) (disable-self))\n";
        env.read = [&text](const std::string&, std::string& out) {
            out = text;
            return true;
        };
        brain.program = AI::Script::Compile("main", env);
        AI::Reset(brain.mind, brain.program.rules.size());
        brain.mind.stance[kComputer] = static_cast<uint8_t>(AI::Lang::Stance::Ally);
        brain.mind.stance[kHuman] = static_cast<uint8_t>(AI::Lang::Stance::Enemy);
    }

    bool boarded = false, landed = false, struck = false;
    for (int t = 0; verdict == nullptr && t < 8 * 60 * Shore::kTicksPerSecond && !struck; ++t) {
        sim.Update(16);
        for (entt::entity k : knights) {
            if (!reg.valid(k)) continue;
            if (const auto* in = reg.try_get<ECS::Components::Garrisoned>(k); in != nullptr && in->building == ship) {
                boarded = true;
            }
            if (const auto* at = reg.try_get<ECS::Components::Transform>(k)) {
                if (Pathfinding::RegionAt(grid, Physics::TileOf(at->position.x), Physics::TileOf(at->position.y)) ==
                    their_island) {
                    landed = true;
                }
            }
        }
        if (const ECS::Events::Log* log = ECS::Events::Read(reg)) {
            for (const auto& e : log->events) {
                if (e.tick != log->tick || e.kind != ECS::Events::Kind::Struck) continue;
                if (e.player != kHuman || !reg.valid(e.other)) continue;
                const auto* owner = reg.try_get<ECS::Components::Owner>(e.other);
                if (owner != nullptr && owner->player_id == kComputer) struck = true;
            }
        }
    }
    if (verdict == nullptr && !boarded) verdict = "the attack group never boarded its transport";
    if (verdict == nullptr && !landed) verdict = "the attack group was never landed on the enemy's island";
    if (verdict == nullptr && !struck) verdict = "the landed attack group never struck";
    std::printf("crossing=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
