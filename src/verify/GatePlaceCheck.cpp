#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "ecs/Buildings.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/Walls.h"

namespace Verify {

// A gate is placed as any building is: on open ground the player has
// explored, it may go up; on open ground it has never explored, or over a
// foundation of its own not yet begun, it may not.
bool CheckGatePlace() {
    const char* verdict = "ok";
    ECS::Simulation sim;
    sim.Init(Core::DefaultSetup());
    sim.Update(16);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    auto& cfg = Core::ConfigManager::Get();
    const int button = cfg.GetUnitIdByName("GTAX2");
    const int house_id = cfg.GetUnitIdByName("HOUS");

    // Open ground, unexplored by player 1, room for a gate and a House.
    int gx = -1, gy = -1;
    for (int y = 8; y < grid.GetHeight() - 8 && gx < 0; ++y) {
        for (int x = 8; x < grid.GetWidth() - 8 && gx < 0; ++x) {
            bool open = true, unseen = true;
            for (int oy = -3; oy <= 3 && open; ++oy) {
                for (int ox = -3; ox <= 3 && open; ++ox) {
                    open = grid.IsWalkable(x + ox, y + oy);
                    unseen = unseen && !ECS::Vision::Explored(reg, 1, x + ox, y + oy);
                }
            }
            if (open && unseen) {
                gx = x;
                gy = y;
            }
        }
    }
    bool unexplored = true, explored = false, over_foundation = true;
    if (gx < 0) {
        verdict = "no unexplored open ground";
    } else {
        const int gate = ECS::Walls::GateFor(reg, 1, button, gx, gy);
        unexplored = ECS::Walls::CanPlaceGate(reg, grid, 1, gate, gx, gy);
        ECS::Vision::Explore(reg, 1, gx, gy, 6);
        explored = ECS::Walls::CanPlaceGate(reg, grid, 1, gate, gx, gy);
        // A House foundation of player 1's, not begun, under the gate.
        if (ECS::Buildings::Place(reg, grid, house_id, 1, gx, gy, false) == entt::null) {
            verdict = "no House foundation laid";
        } else {
            over_foundation = ECS::Walls::CanPlaceGate(reg, grid, 1, gate, gx, gy);
        }
        if (std::strcmp(verdict, "ok") != 0) {
        } else if (unexplored) {
            verdict = "a gate may go up on ground never explored";
        } else if (!explored) {
            verdict = "a gate may not go up on open explored ground";
        } else if (over_foundation) {
            verdict = "a gate may go up over a foundation of its own";
        }
    }
    std::printf("gateplace=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
