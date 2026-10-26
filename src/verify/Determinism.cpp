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
#include "pathfinding/Pathfinder.h"

namespace Verify {

bool CheckDeterminism(int ticks) {
    const int verify_ticks = ticks;
    ECS::Simulation headless;
    headless.Init();

    std::vector<std::pair<entt::entity, std::pair<float, float>>> g_idle_start;
    std::vector<std::pair<entt::entity, std::pair<int, int>>> g_walkers;


    // Give every unit a move order first, so the digest covers the pathfinder
    // and the movement integrator as well as map generation. Destinations are
    // derived from the entity id so the orders are themselves deterministic.
    {
        auto& reg = headless.GetRegistry();
        auto& grid = headless.GetGrid();
        // Two passes: every other unit is designated a stander, then walkers
        // are sent directly at one of them, so the digest covers crowding and
        // separation as well as routing.
        std::vector<entt::entity> units;
        auto movers = reg.view<ECS::Components::Transform, ECS::Components::Worker>();
        for (auto e : movers) units.push_back(e);

        int n = static_cast<int>(units.size()), routed = 0;
        for (size_t i = 1; i < units.size(); i += 2) {
            const auto& t = reg.get<ECS::Components::Transform>(units[i]);
            g_idle_start.emplace_back(units[i],
                std::make_pair(t.position.x.ToFloat(), t.position.y.ToFloat()));
        }

        // The nearest walkable tile to a point, searched outward in a
        // fixed order so the scenario is identical on every run.
        auto nearest_open = [&grid](int x, int y, int& out_x, int& out_y) {
            for (int radius = 0; radius < 24; ++radius) {
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                        if (!grid.IsWalkable(x + dx, y + dy)) continue;
                        out_x = x + dx;
                        out_y = y + dy;
                        return true;
                    }
                }
            }
            return false;
        };

        // Two kinds of order. Some walkers head straight at a unit standing
        // still, the crowding case; the rest cross twenty tiles of open map,
        // rounding whatever forest is in the way, where a unit's distance to
        // its goal may grow for a while.
        static const int kHeadingX[4] = {1, -1, 1, -1};
        static const int kHeadingY[4] = {1, 1, -1, -1};
        for (size_t i = 0; i < units.size(); i += 2) {
            const size_t walker = i / 2;
            const auto& from = reg.get<ECS::Components::Transform>(units[i]);
            const int start_x = from.position.x.ToInt();
            const int start_y = from.position.y.ToInt();

            int goal_x = 0;
            int goal_y = 0;
            if (walker % 2 == 1) {
                // Straight at the nearest stander.
                float best = 0.0f;
                bool have = false;
                for (const auto& stander : g_idle_start) {
                    if (stander.first == units[i]) continue;
                    const auto& to = reg.get<ECS::Components::Transform>(stander.first);
                    const float dx = to.position.x.ToFloat() - from.position.x.ToFloat();
                    const float dy = to.position.y.ToFloat() - from.position.y.ToFloat();
                    const float d2 = dx * dx + dy * dy;
                    if (!have || d2 < best) {
                        have = true;
                        best = d2;
                        goal_x = to.position.x.ToInt();
                        goal_y = to.position.y.ToInt();
                    }
                }
                if (!have) continue;
            }

            // A long walk tries its own heading first and the others after,
            // in a fixed order, so where a start happens to sit -- against the
            // map edge, behind a lake -- never quietly drops a walker.
            ECS::Components::Path p;
            for (size_t turn = 0; turn < 4 && p.waypoints.empty(); ++turn) {
                if (walker % 2 == 0) {
                    const size_t heading = (walker + turn) % 4;
                    if (!nearest_open(start_x + kHeadingX[heading] * 20,
                                      start_y + kHeadingY[heading] * 20,
                                      goal_x, goal_y)) {
                        continue;
                    }
                } else if (turn > 0) {
                    break;
                }
                p = Pathfinding::AStar::FindPath(grid, start_x, start_y, goal_x, goal_y);
            }
            if (p.waypoints.empty()) continue;
            reg.emplace_or_replace<ECS::Components::Path>(units[i], std::move(p));
            g_walkers.emplace_back(units[i], std::make_pair(goal_x, goal_y));
            ++routed;
        }

        std::printf("verify: %d units, %d ordered, %zu holding position\n",
                    n, routed, g_idle_start.size());
    }

    // FNV-1a over every entity's fixed-point position, in entity order, sampled
    // during the run as well as at the end: units reach their destinations well
    // before the last tick, so the final state alone cannot tell two
    // trajectories apart.
    auto& reg = headless.GetRegistry();
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            hash ^= static_cast<uint8_t>(v >> (b * 8));
            hash *= 1099511628211ull;
        }
    };

    std::vector<std::pair<uint32_t, const ECS::Components::Transform*>> rows;
    auto sample = [&]() {
        rows.clear();
        reg.view<ECS::Components::Transform>().each(
            [&](auto e, const ECS::Components::Transform& t) {
                rows.emplace_back(static_cast<uint32_t>(entt::to_integral(e)), &t);
            });
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& row : rows) {
            mix(static_cast<uint64_t>(row.second->position.x.value));
            mix(static_cast<uint64_t>(row.second->position.y.value));
        }
    };

    constexpr int kSampleEvery = 25;
    for (int t = 0; t < verify_ticks; ++t) {
        headless.Update(16);
        if ((t % kSampleEvery) == 0) sample();
    }
    sample();

    // Arrival. Units converging on one spot cannot all stand on it, so
    // "arrived" means close enough to be crowding it rather than exactly on
    // it; anything further away stopped somewhere it should not have.
    constexpr float kArrivedWithin = 2.5f;
    int arrived = 0;
    float worst = 0.0f;
    for (const auto& walker : g_walkers) {
        if (!reg.valid(walker.first)) continue;
        const auto& t = reg.get<ECS::Components::Transform>(walker.first);
        const float dx = t.position.x.ToFloat() - static_cast<float>(walker.second.first);
        const float dy = t.position.y.ToFloat() - static_cast<float>(walker.second.second);
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= kArrivedWithin) ++arrived;
        if (distance > worst) worst = distance;
    }
    std::printf("arrived=%d/%zu worst=%.2f tiles\n",
                arrived, g_walkers.size(), worst);

    std::printf("entities=%zu ticks=%d hash=%016llx\n",
                rows.size(), verify_ticks,
                static_cast<unsigned long long>(hash));
    return arrived == static_cast<int>(g_walkers.size());
}

} // namespace Verify
