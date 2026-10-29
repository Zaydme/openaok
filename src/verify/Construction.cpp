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
#include "ecs/Units.h"
#include "ecs/components/Owner.h"
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

bool CheckConstruction() {
    ECS::Simulation headless;
    headless.Init();
    auto& reg = headless.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    auto& build_grid = headless.GetGrid();

    // Construction and demolition, end to end. A crew raises a Town Center
    // beside their own; the site has to finish as real building art that blocks
    // its ground, and deleting it has to give the ground back and leave rubble.

    // The Town Center is a Castle Age building and costs more than a player
    // starts with, so the player is given it and the wood up front.
    const int town_centre = cfg.GetUnitIdByName("RTWC1X");
    if (auto* me = ECS::Players::Find(reg, Core::PLAYER_LOCAL)) {
        if (town_centre >= 0) me->units[town_centre].enabled = true;
        ECS::Players::AddStock(*me, Core::ResourceType::Wood, 1000);
    }

    // A crew of six of the player's own villagers: the three it starts with and
    // three more beside them.
    std::vector<entt::entity> villagers;
    {
        auto workers = reg.view<ECS::Components::Worker, ECS::Components::Transform,
                                ECS::Components::Owner>();
        for (auto candidate : workers) {
            if (workers.get<ECS::Components::Owner>(candidate).player_id ==
                Core::PLAYER_LOCAL) {
                villagers.push_back(candidate);
            }
        }
        std::sort(villagers.begin(), villagers.end());
        if (!villagers.empty()) {
            const auto first = reg.get<ECS::Components::Transform>(villagers[0]).position;
            const int extra_id = cfg.GetUnitIdByName("VMBAS");
            // Beside the first, off the tile lattice the way every unit
            // stands, so where they end up building says something about the
            // planner and nothing about where they were put down.
            static const int kExtraX[3] = {1, 0, 1};
            static const int kExtraY[3] = {0, 1, 1};
            const Core::Fixed32 off = Core::Fixed32(3) / Core::Fixed32(10);
            for (int i = 0; i < 3; ++i) {
                const Core::Fixed32 x = first.x + Core::Fixed32(kExtraX[i]) + off;
                const Core::Fixed32 y = first.y + Core::Fixed32(kExtraY[i]) - off;
                if (!build_grid.IsWalkable(x.ToInt(), y.ToInt())) continue;
                const entt::entity extra = ECS::Units::Spawn(
                    reg, extra_id, Core::PLAYER_LOCAL, {x, y});
                if (extra != entt::null) villagers.push_back(extra);
            }
        }
    }
    entt::entity builder = villagers.empty() ? entt::null : villagers[0];

    const char* verdict = "no builder";
    if (builder != entt::null) {
        const auto& at = reg.get<ECS::Components::Transform>(builder);
        int site_x = at.position.x.ToInt();
        const int site_y = at.position.y.ToInt();
        // A Town Center: the widest footprint, and slow enough to raise that
        // the whole crew works the site at once. Open ground a few tiles away,
        // found the same way every run, with the whole footprint clear.
        constexpr int kSiteHalf = 2;
        for (int radius = 5; radius < 20; ++radius) {
            bool clear = true;
            for (int oy = -kSiteHalf; oy <= kSiteHalf && clear; ++oy) {
                for (int ox = -kSiteHalf; ox <= kSiteHalf; ++ox) {
                    if (!build_grid.IsWalkable(site_x + radius + ox,
                                               site_y + oy)) {
                        clear = false;
                        break;
                    }
                }
            }
            if (clear) {
                site_x += radius;
                break;
            }
        }

        // Every villager is sent, so a crew works one site.
        int ordered_builders = 0;
        // Where each villager stood when it was told to build, to check it
        // walked to the near wall.
        std::vector<std::pair<entt::entity, std::pair<float, float>>> from;
        for (auto member : villagers) {
            ++ordered_builders;
            const auto& at = reg.get<ECS::Components::Transform>(member);
            from.push_back({member, {at.position.x.ToFloat(),
                                     at.position.y.ToFloat()}});
            Core::CommandQueue::Get().PushCommand(
                Core::BuildCommand{member, town_centre, site_x, site_y,
                                   Core::PLAYER_LOCAL});
        }

        // Watched the whole time the site is up, since a crew fighting over one
        // standing spot has settled by the time the building finishes. Two
        // claims on one tile, or two villagers hammering from inside each
        // other, fail.
        const char* crowd_fault = nullptr;
        int most_at_once = 0;
        bool crew_settled = false;
        // The one builder pulled off the job part way through, and what
        // it did wrong afterwards.
        entt::entity interrupted = entt::null;
        const char* interrupt_fault = nullptr;

        entt::entity house = entt::null;
        // Long enough for the crew to walk over and raise it at the dat's own
        // build time: a crew of six puts a Town Center up in a little under
        // forty seconds of simulation, so two minutes is ample.
        for (int t = 0; t < 7500 && house == entt::null; ++t) {
            headless.Update(16);
            // Interrupt one builder mid-job, once: it has to drop `working`,
            // the build graphic set and any work or combat animation. `working`
            // is what makes the animation system wear the job graphic, and a
            // move order that ends a build is handled in Simulation, not
            // WorkerSystem.
            if (interrupted == entt::null) {
                auto crew = reg.view<ECS::Components::Worker,
                                     ECS::Components::Transform>();
                for (auto member : crew) {
                    if (!crew.get<ECS::Components::Worker>(member).working) {
                        continue;
                    }
                    const auto& at =
                        crew.get<ECS::Components::Transform>(member);
                    interrupted = member;
                    Core::CommandQueue::Get().PushCommand(Core::MoveCommand{
                        member, at.position.x - Core::Fixed32(6),
                        at.position.y, Core::PLAYER_LOCAL});
                    break;
                }
            } else if (interrupt_fault == nullptr &&
                       reg.valid(interrupted)) {
                const auto& w = reg.get<ECS::Components::Worker>(interrupted);
                const auto& an =
                    reg.get<ECS::Components::Animation>(interrupted);
                if (w.working) {
                    interrupt_fault = "interrupted builder kept working";
                } else if (an.job == ECS::Components::VillagerJob::Build) {
                    interrupt_fault = "interrupted builder kept the build "
                                      "graphic set";
                } else if (an.state == ECS::Components::AnimState::Work ||
                           an.state == ECS::Components::AnimState::Attack) {
                    interrupt_fault = "interrupted builder plays a work or "
                                      "combat animation";
                }
            }

            // Two villagers must never be walking to the same place. A selected
            // group stands close together, so every member approaches on the
            // same bearing; the caller has to tell the planner what its
            // crewmates are already heading for.
            if (crowd_fault == nullptr) {
                auto going = reg.view<ECS::Components::Worker,
                                      ECS::Components::Path>();
                for (auto p : going) {
                    const auto& wp = going.get<ECS::Components::Worker>(p);
                    if (wp.state != Core::UnitState::Building) continue;
                    const auto& pp = going.get<ECS::Components::Path>(p);
                    if (!pp.has_goal) continue;
                    for (auto q : going) {
                        if (q <= p) continue;
                        const auto& wq = going.get<ECS::Components::Worker>(q);
                        if (wq.state != Core::UnitState::Building) continue;
                        if (wq.target_building != wp.target_building) continue;
                        const auto& qq = going.get<ECS::Components::Path>(q);
                        if (!qq.has_goal) continue;
                        const float dx = pp.goal.x.ToFloat() - qq.goal.x.ToFloat();
                        const float dy = pp.goal.y.ToFloat() - qq.goal.y.ToFloat();
                        // The planner keeps destinations three quarters of
                        // a villager's width apart (0.3 tiles); two closer
                        // than that, by more than the rounding of reading
                        // fixed point back as float, were handed the same
                        // spot.
                        if (dx * dx + dy * dy < 0.29f * 0.29f) {
                            crowd_fault = "two builders sent to the same "
                                          "spot";
                        }
                    }
                }
            }

            if (crowd_fault == nullptr) {
                int hammering = 0;
                auto builders = reg.view<ECS::Components::Worker,
                                         ECS::Components::Transform>();
                for (auto a : builders) {
                    // First fault wins, so the verdict names the first thing
                    // wrong.
                    if (crowd_fault != nullptr) break;
                    const auto& wa = builders.get<ECS::Components::Worker>(a);
                    if (wa.state != Core::UnitState::Building) continue;

                    if (wa.working) {
                        ++hammering;
                        const auto& ta =
                            builders.get<ECS::Components::Transform>(a);

                        // Where a builder stands is a position, not a tile:
                        // whole coordinates or dead tile centres on both axes
                        // fail.
                        const float fx = ta.position.x.ToFloat();
                        const float fy = ta.position.y.ToFloat();
                        const float rx = fx - std::floor(fx);
                        const float ry = fy - std::floor(fy);
                        const bool lattice_x = rx < 0.02f || rx > 0.98f ||
                                               std::fabs(rx - 0.5f) < 0.02f;
                        const bool lattice_y = ry < 0.02f || ry > 0.98f ||
                                               std::fabs(ry - 0.5f) < 0.02f;
                        if (lattice_x && lattice_y) {
                            crowd_fault = "builder standing on the grid";
                        }

                        // It has to have walked to the near wall, not round the
                        // site.
                        for (const auto& start : from) {
                            if (start.first != a) continue;
                            const float ax = start.second.first -
                                             static_cast<float>(site_x);
                            const float ay = start.second.second -
                                             static_cast<float>(site_y);
                            const float bx = fx - static_cast<float>(site_x);
                            const float by = fy - static_cast<float>(site_y);
                            // Same side of the site: the direction it came from
                            // and the direction it stands in must not be
                            // opposed. A builder on the neighbouring face
                            // because its own is full is fine; one past the
                            // corners on the far side is not.
                            const float la = std::sqrt(ax * ax + ay * ay);
                            const float lb = std::sqrt(bx * bx + by * by);
                            const float cosine =
                                (la > 0.0f && lb > 0.0f)
                                    ? (ax * bx + ay * by) / (la * lb) : 1.0f;
                            if (cosine < -0.5f) {
                                crowd_fault = "builder walked round to "
                                              "the far side of the site";
                            }
                            break;
                        }

                        // A villager swinging a hammer has to be wearing the
                        // build set: that set's own loop is the hammering.
                        const auto& an =
                            reg.get<ECS::Components::Animation>(a);
                        const int work = static_cast<int>(
                            ECS::Components::AnimState::Work);
                        const auto& sp =
                            reg.get<ECS::Components::Sprite>(a);
                        if (an.job != ECS::Components::VillagerJob::Build) {
                            crowd_fault = "hammering without the build "
                                          "graphic set";
                        } else if (an.slp[work] < 0 || an.frames[work] <= 0) {
                            crowd_fault = "build set has no work "
                                          "animation";
                        } else if (an.slp[work] ==
                                   an.slp[static_cast<int>(
                                       ECS::Components::AnimState::Idle)]) {
                            // The work animation is not in the unit record;
                            // read from there it would be the standing graphic.
                            crowd_fault = "work animation is the standing "
                                          "graphic";
                        } else if (an.state !=
                                   ECS::Components::AnimState::Work) {
                            crowd_fault = "hammering but not playing the "
                                          "work animation";
                        } else if (sp.slp_id != an.slp[work]) {
                            crowd_fault = "work animation chosen but not "
                                          "on the sprite";
                        }
                    }
                    for (auto b : builders) {
                        if (crowd_fault != nullptr) break;
                        if (b <= a) continue;
                        const auto& wb =
                            builders.get<ECS::Components::Worker>(b);
                        if (wb.target_building != wa.target_building) continue;
                        if (!wa.working || !wb.working) continue;
                        const auto& ta =
                            builders.get<ECS::Components::Transform>(a);
                        const auto& tb =
                            builders.get<ECS::Components::Transform>(b);
                        const float dx = ta.position.x.ToFloat() -
                                         tb.position.x.ToFloat();
                        const float dy = ta.position.y.ToFloat() -
                                         tb.position.y.ToFloat();
                        // Builders may overlap heavily; only two villagers on
                        // the same point is wrong.
                        if (dx * dx + dy * dy < 0.02f) {
                            crowd_fault = "two builders stacked on one "
                                          "point";
                        }
                    }
                }
                if (hammering > most_at_once) most_at_once = hammering;
                // Nearly the whole crew has to be working at the same time. A
                // crew that never gets more than a couple of hammers going is
                // circling the site swapping places.
                if (most_at_once >= ordered_builders - 2) {
                    crew_settled = true;
                }
            }

            auto sites = reg.view<ECS::Components::Building,
                                  ECS::Components::Transform>();
            for (auto candidate : sites) {
                if (reg.get<ECS::Components::Building>(candidate).state !=
                    ECS::Components::BuildingState::Complete) {
                    continue;
                }
                const auto& transform =
                    reg.get<ECS::Components::Transform>(candidate);
                if (transform.position.x.ToInt() == site_x &&
                    transform.position.y.ToInt() == site_y) {
                    house = candidate;
                    break;
                }
            }
        }

        // Let the crew notice the job is over: a villager that already ran this
        // tick is released on the next one.
        for (int settle = 0; settle < 4 && house != entt::null; ++settle) {
            headless.Update(16);
        }

        if (house == entt::null) {
            // Report where the builder is and what it is doing.
            const auto& w = reg.get<ECS::Components::Worker>(builder);
            const auto& wt = reg.get<ECS::Components::Transform>(builder);
            std::printf("  site=(%d,%d) worker=(%.2f,%.2f) state=%d "
                        "has_target=%d\n", site_x, site_y,
                        wt.position.x.ToFloat(), wt.position.y.ToFloat(),
                        static_cast<int>(w.state),
                        reg.valid(w.target_building) ? 1 : 0);
            verdict = "never finished";
        } else if (!reg.all_of<ECS::Components::Sprite>(house)) {
            verdict = "finished with no art";
        } else if (build_grid.IsWalkable(site_x, site_y)) {
            verdict = "finished but does not block its ground";
        } else {
            // How close the builder ended up standing, and how many
            // villagers actually joined in.
            int builders = 0;
            float nearest = 99.0f;
            auto crew = reg.view<ECS::Components::Worker,
                                 ECS::Components::Transform>();
            for (auto member : crew) {
                const auto& at = crew.get<ECS::Components::Transform>(member);
                const float dx = at.position.x.ToFloat() - static_cast<float>(site_x);
                const float dy = at.position.y.ToFloat() - static_cast<float>(site_y);
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance < 6.0f) { ++builders; }
                if (distance < nearest) nearest = distance;
            }
            std::printf("  builder stood %.2f tiles from the site centre, "
                        "%d villagers within reach, %d hammering at once\n",
                        nearest, builders, most_at_once);

            // Nobody may be left wearing a job graphic once the job is
            // over: a villager that finishes a house and keeps the
            // build set on stands there swinging at nothing, and one
            // left on a gathering set mimes hauling wood it never cut.
            const char* animation_fault = crowd_fault;
            if (animation_fault == nullptr) animation_fault = interrupt_fault;
            if (animation_fault == nullptr && interrupted == entt::null) {
                animation_fault = "no builder was ever interrupted";
            }
            if (animation_fault == nullptr && !crew_settled) {
                animation_fault = "crew never settled around the site";
            }
            static char fault_detail[144];
            for (auto member : crew) {
                if (animation_fault != nullptr) break;
                // Only villagers that have stopped. One still walking
                // is correctly playing its walk, whatever it was sent
                // to do.
                if (const auto* route =
                        reg.try_get<ECS::Components::Path>(member)) {
                    if (route->current_waypoint_index <
                        route->waypoints.size()) {
                        continue;
                    }
                }
                const auto& an = reg.get<ECS::Components::Animation>(member);
                const auto& wk = crew.get<ECS::Components::Worker>(member);
                if (an.job == ECS::Components::VillagerJob::Base &&
                    an.state == ECS::Components::AnimState::Idle) {
                    continue;
                }
                std::snprintf(fault_detail, sizeof(fault_detail),
                              "villager left standing mid-job "
                              "(job=%d anim=%d worker_state=%d working=%d)",
                              static_cast<int>(an.job),
                              static_cast<int>(an.state),
                              static_cast<int>(wk.state),
                              wk.working ? 1 : 0);
                animation_fault = fault_detail;
                break;
            }

            if (animation_fault != nullptr) {
                verdict = animation_fault;
            } else {
                Core::CommandQueue::Get().PushCommand(
                    Core::DeleteCommand{house, Core::PLAYER_LOCAL});
                headless.Update(16);
                if (reg.valid(house)) {
                    verdict = "delete left the building standing";
                } else if (!build_grid.IsWalkable(site_x, site_y)) {
                    verdict = "delete left the ground blocked";
                } else {
                    // Rubble: the dat names a wreck unit per building,
                    // and a razed building should leave one behind.
                    bool rubble = false;
                    auto leftovers = reg.view<ECS::Components::Sprite,
                                              ECS::Components::Transform>();
                    for (auto leftover : leftovers) {
                        const auto& at =
                            leftovers.get<ECS::Components::Transform>(leftover);
                        if (at.position.x.ToInt() == site_x &&
                            at.position.y.ToInt() == site_y) {
                            rubble = true;
                            break;
                        }
                    }
                    verdict = rubble ? "ok" : "no rubble left behind";
                }
            }
        }
    }
    (void)cfg;
    std::printf("building=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
