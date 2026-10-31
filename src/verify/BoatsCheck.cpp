#include "verify/Harness.h"

#include <cstdio>
#include <vector>

#include "ai/Brain.h"
#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using SN = AI::Lang::SN;

// A computer player on Islands with a finished Dock, its rules doing
// nothing, and `ships` of `unit_id` launched beside the Dock.
struct Harbour {
    ECS::Simulation sim;
    Core::GameSetup setup;
    AI::Brain* brain = nullptr;
    entt::entity dock = entt::null;
    std::vector<entt::entity> ships;

    const char* Open(int unit_id, int count) {
        setup = Core::DefaultSetup();
        setup.map_script = "Islands";
        setup.difficulty = 0;
        setup.players[1].computer = true;
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
        const auto [sx, sy] = sim.player_starts[0];
        const int dock_id = Core::ConfigManager::Get().GetUnitIdByName("DOCK");
        dock = Shore::RaiseNear(sim, 1, dock_id, sx, sy, 40);
        if (dock == entt::null) return "no room for a Dock";
        const Physics::Layer sea = Shore::Sea(sim);
        const auto& at = reg.get<ECS::Components::Transform>(dock).position;
        const int dx = Physics::TileOf(at.x), dy = Physics::TileOf(at.y);
        for (int r = 2; r < 12 && static_cast<int>(ships.size()) < count; ++r) {
            for (int oy = -r; oy <= r && static_cast<int>(ships.size()) < count; ++oy) {
                for (int ox = -r; ox <= r && static_cast<int>(ships.size()) < count; ox += 2) {
                    const int x = dx + ox, y = dy + oy;
                    if (!grid.InBounds(x, y) || !grid.Passable(sea, x, y) || grid.IsWalkable(x, y)) continue;
                    const entt::entity e =
                        ECS::ScenarioWorld::Place(reg, grid, unit_id, 1, Shore::At(x, y), {}, 0);
                    if (e != entt::null) ships.push_back(e);
                }
            }
        }
        if (static_cast<int>(ships.size()) != count) return "no water beside the Dock for the ships";
        return nullptr;
    }

    void Run(int seconds) {
        for (int t = 0; t < seconds * Shore::kTicksPerSecond; ++t) sim.Update(16);
    }
};

bool Sailing(const entt::registry& reg, entt::entity e) {
    const auto* path = reg.try_get<ECS::Components::Path>(e);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size();
}

// A fishing ship by the Dock: with sn-maximum-fish-boat-drop-distance 1 no
// fish is near enough and it waits; with no limit it goes fishing.
const char* CheckFishLimit() {
    for (const int limit : {1, -1}) {
        Harbour h;
        if (const char* v = h.Open(Core::ConfigManager::Get().GetUnitIdByName("FSHSP"), 1)) return v;
        AI::SetStrategic(h.brain->mind, SN::MaximumFishBoatDropDistance, limit, &h.setup);
        h.Run(20);
        const auto& w = h.sim.GetRegistry().get<ECS::Components::Worker>(h.ships.front());
        const bool fishing = w.target_resource != entt::null;
        if (limit >= 0 && fishing) return "a fishing ship went for fish further from the Dock than the rules allow";
        if (limit < 0 && !fishing) return "a fishing ship with no limit on its fish never went fishing";
    }
    return nullptr;
}

// Three galleys at the Hardest: two boat attack groups of one take two, and
// the third explores the water; asked for no boat explore groups, none does.
const char* CheckBoatExplorers() {
    for (const int groups : {1, 0}) {
        Harbour h;
        if (const char* v = h.Open(Core::ConfigManager::Get().GetUnitIdByName("SGALY"), 3)) return v;
        AI::SetStrategic(h.brain->mind, SN::NumberBoatExploreGroups, groups, &h.setup);
        h.Run(5);
        const auto& reg = h.sim.GetRegistry();
        size_t members = 0;
        for (const auto& group : h.brain->boat_explore_groups) members += group.size();
        int sailing = 0;
        for (entt::entity ship : h.ships) sailing += Sailing(reg, ship) ? 1 : 0;
        if (groups == 1) {
            if (h.brain->boat_explore_groups.size() != 1 || members != 1) {
                return "the warship the boat attack groups leave over was not sent exploring";
            }
            if (sailing != 1) return "the exploring warship did not sail";
        } else if (members != 0 || sailing != 0) {
            return "a warship went exploring with no boat explore group asked for";
        }
    }
    return nullptr;
}

} // namespace

bool CheckBoats() {
    const char* verdict = CheckFishLimit();
    if (verdict == nullptr) verdict = CheckBoatExplorers();
    std::printf("boats=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
