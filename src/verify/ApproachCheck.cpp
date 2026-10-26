#include "verify/Harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "ecs/Players.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Simulation.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Path.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/ApproachPlanner.h"
#include "core/ConfigManager.h"
#include "pathfinding/Pathfinder.h"

namespace Verify {

bool CheckApproachPlanner() {
    ECS::Simulation headless;
    headless.Init();

    // The approach planner on its own: ground in, a standing spot out.
    const auto& plan_grid = headless.GetGrid();
    const char* planner = "ok";

    // A patch of open ground to plan against.
    int cx = 0;
    int cy = 0;
    for (int y = 8; y < 200 && cx == 0; ++y) {
        for (int x = 8; x < 200; ++x) {
            bool clear = true;
            for (int oy = -4; oy <= 4 && clear; ++oy) {
                for (int ox = -4; ox <= 4; ++ox) {
                    if (!plan_grid.IsWalkable(x + ox, y + oy)) { clear = false; break; }
                }
            }
            if (clear) { cx = x; cy = y; break; }
        }
    }

    const ECS::Buildings::Bounds ground = ECS::Buildings::FootprintBounds(
        {Core::Fixed32(cx), Core::Fixed32(cy)}, /*Town Center*/ 109);
    const Core::Fixed32 standoff = Core::Fixed32(45) / Core::Fixed32(100);
    const Core::Fixed32 clearance = Core::Fixed32(7) / Core::Fixed32(10);

    auto ask = [&](float fx, float fy,
                   const ECS::Components::Vector2D* taken, size_t count) {
        Pathfinding::ApproachRequest r;
        r.target = ground;
        r.from = {Core::Fixed32(fx), Core::Fixed32(fy)};
        r.standoff = standoff;
        r.clearance = clearance;
        r.taken = taken;
        r.taken_count = count;
        return Pathfinding::FindStandingSpot(plan_grid, r);
    };

    // Several callers approaching from the same quarter, at slightly different
    // bearings, get different spots. They are asked one at a time, each told
    // what the earlier ones took, as the worker system calls it; callers far
    // off to one side all want the same nearest point, and the clearance
    // spreads them.
    std::vector<ECS::Components::Vector2D> claimed;
    const auto a = ask(cx - 30.0f, cy - 14.0f, nullptr, 0);
    claimed.push_back(a.spot);
    const auto b = ask(cx - 30.0f, cy - 20.0f, claimed.data(), claimed.size());
    claimed.push_back(b.spot);
    const auto c = ask(cx - 30.0f, cy - 26.0f, claimed.data(), claimed.size());
    if (!a.found || !b.found || !c.found) {
        planner = "no spot on open ground";
    } else {
        auto same = [](const ECS::Components::Vector2D& p,
                       const ECS::Components::Vector2D& q) {
            const float dx = p.x.ToFloat() - q.x.ToFloat();
            const float dy = p.y.ToFloat() - q.y.ToFloat();
            return dx * dx + dy * dy < 0.0004f;
        };
        if (same(a.spot, b.spot) || same(b.spot, c.spot) ||
            same(a.spot, c.spot)) {
            planner = "different bearings collapse to one spot";
        }
    }

    // The spot must sit at the standoff from the ground, not inside it
    // and not a stride off.
    if (std::strcmp(planner, "ok") == 0) {
        const float off = ground.DistanceFrom(a.spot).ToFloat();
        if (std::fabs(off - standoff.ToFloat()) > 0.01f) {
            planner = "spot is not at the standoff";
        }
    }

    // An occupied spot must be given up rather than shared.
    if (std::strcmp(planner, "ok") == 0) {
        const ECS::Components::Vector2D taken[] = {a.spot};
        const auto moved = ask(cx - 30.0f, cy - 14.0f, taken, 1);
        const float dx = moved.spot.x.ToFloat() - a.spot.x.ToFloat();
        const float dy = moved.spot.y.ToFloat() - a.spot.y.ToFloat();
        if (!moved.found) {
            planner = "no free spot beside an occupied one";
        } else if (dx * dx + dy * dy < clearance.ToFloat() * clearance.ToFloat() - 0.01f) {
            planner = "spot handed out on top of an occupied one";
        }
    }
    // The rectangle's own geometry, which every spot is measured from. A
    // footprint with an even tile count is anchored up and left of its origin
    // tile, not centred on it. A caller due east of a building gets a spot
    // squarely on the east face, level with the footprint's true middle, and
    // the four cardinal spots are symmetric about it.
    if (std::strcmp(planner, "ok") == 0) {
        const float mid_x = (ground.min_x.ToFloat() + ground.max_x.ToFloat()) * 0.5f;
        const float mid_y = (ground.min_y.ToFloat() + ground.max_y.ToFloat()) * 0.5f;

        const auto east  = ask(mid_x + 40.0f, mid_y, nullptr, 0);
        const auto west  = ask(mid_x - 40.0f, mid_y, nullptr, 0);
        const auto south = ask(mid_x, mid_y + 40.0f, nullptr, 0);
        const auto north = ask(mid_x, mid_y - 40.0f, nullptr, 0);

        if (!east.found || !west.found || !south.found || !north.found) {
            planner = "no spot for a cardinal approach";
        } else if (std::fabs(east.spot.y.ToFloat() - mid_y) > 0.02f ||
                   std::fabs(west.spot.y.ToFloat() - mid_y) > 0.02f) {
            planner = "east/west spot is not level with the middle";
        } else if (std::fabs(south.spot.x.ToFloat() - mid_x) > 0.02f ||
                   std::fabs(north.spot.x.ToFloat() - mid_x) > 0.02f) {
            planner = "north/south spot is not level with the middle";
        } else {
            // Symmetric about the true centre, which is only true if the
            // centre really is the middle of the footprint.
            const float dx_e = east.spot.x.ToFloat() - mid_x;
            const float dx_w = mid_x - west.spot.x.ToFloat();
            const float dy_s = south.spot.y.ToFloat() - mid_y;
            const float dy_n = mid_y - north.spot.y.ToFloat();
            if (std::fabs(dx_e - dx_w) > 0.02f || std::fabs(dy_s - dy_n) > 0.02f) {
                planner = "spots are not symmetric about the centre";
            } else if (std::fabs(dx_e - dy_s) > 0.02f) {
                planner = "a square footprint is not reached squarely";
            }
        }
    }

    // A caller on the exact diagonal gets a spot on a face, clear of the
    // corner. The four world diagonals are the player's up, down, left and
    // right, so most approaches lie on one.
    if (std::strcmp(planner, "ok") == 0) {
        const float mid_x = (ground.min_x.ToFloat() + ground.max_x.ToFloat()) * 0.5f;
        const float mid_y = (ground.min_y.ToFloat() + ground.max_y.ToFloat()) * 0.5f;
        const float half = (ground.max_x.ToFloat() - ground.min_x.ToFloat()) * 0.5f
                         + standoff.ToFloat();

        for (int corner = 0; corner < 4; ++corner) {
            const float sx = (corner & 1) ? 1.0f : -1.0f;
            const float sy = (corner & 2) ? 1.0f : -1.0f;
            const auto spot = ask(mid_x + sx * 30.0f, mid_y + sy * 30.0f,
                                  nullptr, 0);
            if (!spot.found) { planner = "no spot on the diagonal"; break; }
            const float ox = std::fabs(spot.spot.x.ToFloat() - mid_x);
            const float oy = std::fabs(spot.spot.y.ToFloat() - mid_y);
            // Pinned to one face, and not also at the far end of that face.
            const bool on_face = std::fabs(ox - half) < 0.02f ||
                                 std::fabs(oy - half) < 0.02f;
            const bool at_corner = std::fabs(ox - half) < 0.02f &&
                                   std::fabs(oy - half) < 0.02f;
            if (!on_face) { planner = "diagonal spot is not on a face"; break; }
            if (at_corner) { planner = "diagonal spot sits on the corner"; break; }
        }
    }

    // A crowd approaching from one side fills the near face. The spread steps
    // along the perimeter, which runs round corners, so a crowd sent onto the
    // side faces fails.
    if (std::strcmp(planner, "ok") == 0) {
        const float mid_x = (ground.min_x.ToFloat() + ground.max_x.ToFloat()) * 0.5f;
        const float mid_y = (ground.min_y.ToFloat() + ground.max_y.ToFloat()) * 0.5f;
        const float near_x = ground.min_x.ToFloat() - standoff.ToFloat();

        std::vector<ECS::Components::Vector2D> held;
        int on_near_face = 0;
        for (int i = 0; i < 5; ++i) {
            // Five callers, all well to the west, spread a little in y.
            const auto spot = ask(mid_x - 30.0f, mid_y - 2.0f + i * 1.0f,
                                  held.empty() ? nullptr : held.data(),
                                  held.size());
            if (!spot.found) { planner = "no spot for a crowd on one side"; break; }
            if (std::fabs(spot.spot.x.ToFloat() - near_x) < 0.02f) ++on_near_face;
            held.push_back(spot.spot);
        }
        // The west face is long enough to hold all five at this clearance, so
        // every one of them should be on it.
        if (std::strcmp(planner, "ok") == 0 && on_near_face < 5) {
            planner = "a crowd on one side was sent round the corners";
        }
    }

    std::printf("planner=%s\n", planner);
    return std::strcmp(planner, "ok") == 0;
}

} // namespace Verify
