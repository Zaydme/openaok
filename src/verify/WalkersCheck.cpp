#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/GameSetup.h"
#include "ecs/Simulation.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"

namespace Verify {

namespace {

constexpr int kTicksPerSecond = 1000 / 16;
constexpr int kMinutes = 10;
// How long a walker may stand still on its way before it counts as stuck.
constexpr int kStuckAfter = 10;

// Four computer players on `map` for kMinutes: the seconds villagers spent
// standing still on their way for kStuckAfter seconds and more.
long StuckSeconds(const char* map) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = map;
    setup.map_size = 144;
    for (int p = 1; p <= 4; ++p) {
        setup.players[p].active = true;
        setup.players[p].computer = true;
        setup.players[p].civ = 1 + p * 3 % 13;
    }
    setup.difficulty = 0;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    struct Last {
        entt::entity entity = entt::null;
        Core::Fixed32 x, y;
        int still = 0;
    };
    std::vector<Last> last;
    long stuck = 0;
    for (int s = 0; s < kMinutes * 60; ++s) {
        for (int t = 0; t < kTicksPerSecond; ++t) sim.Update(16);
        auto walkers = reg.view<const ECS::Components::Transform, const ECS::Components::Path, const ECS::Components::Worker>();
        for (auto e : walkers) {
            const auto& path = walkers.get<const ECS::Components::Path>(e);
            const auto& at = walkers.get<const ECS::Components::Transform>(e).position;
            const auto index = static_cast<size_t>(entt::to_entity(e));
            if (index >= last.size()) last.resize(index + 1);
            Last& l = last[index];
            const bool walking = path.current_waypoint_index < path.waypoints.size();
            l.still = l.entity == e && walking && l.x == at.x && l.y == at.y ? l.still + 1 : 0;
            l.entity = e;
            l.x = at.x;
            l.y = at.y;
            if (l.still >= kStuckAfter) ++stuck;
        }
    }
    return stuck;
}

} // namespace

// Villagers do not stand short of their places: four computer players for ten
// minutes on Arabia and on Islands, none of their villagers stands still on
// its way for ten seconds or more, but for a few seconds in all -- one whose
// place is a body's width from a crew mate at work, diagonally, walks up to
// it rather than stopping short of it for good.
bool CheckWalkers() {
    const long arabia = StuckSeconds("Arabia");
    const long islands = StuckSeconds("Islands");
    const char* verdict = arabia + islands > 20 ? "villagers stood still on their way to work" : "ok";
    std::printf("walkers=%s arabia_stuck_seconds=%ld islands_stuck_seconds=%ld\n", verdict, arabia, islands);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
