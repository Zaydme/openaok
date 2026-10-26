#include "verify/Harness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <string>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "ecs/Players.h"
#include "ecs/Buildings.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Resources.h"
#include "core/Rules.h"
#include "core/FixedMath.h"
#include "ecs/components/Building.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/DropSites.h"
#include "ecs/Selection.h"
#include "ecs/VillagerJobs.h"
#include "ecs/Simulation.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "pathfinding/ApproachPlanner.h"
#include "core/ConfigManager.h"
#include "pathfinding/Pathfinder.h"
#include "verify/Shore.h"

namespace Verify {

bool CheckGathering() {
    ECS::Simulation headless;
    headless.Init();

    // Gathering, end to end: a villager is sent at a resource, has to reach it,
    // take from it, carry the load to a drop site and hand it over. The run
    // follows the whole round trip and says which leg broke.
    auto& reg = headless.GetRegistry();
    const char* verdict = "no villager";

    // One of the player's own villagers: orders are checked against their
    // owner.
    entt::entity gatherer = entt::null;
    auto villagers = reg.view<ECS::Components::Worker, ECS::Components::Transform,
                              ECS::Components::Owner>();
    for (auto candidate : villagers) {
        if (villagers.get<ECS::Components::Owner>(candidate).player_id ==
            Core::PLAYER_LOCAL) {
            gatherer = candidate;
            break;
        }
    }

    if (gatherer != entt::null) {
        const auto& at = reg.get<ECS::Components::Transform>(gatherer);

        // The nearest resource to that villager, so the walk is short
        // enough to finish inside the tick budget -- one that stands still.
        // A sheep or a deer is a hunt first and a gather after, with its own
        // check in the combat run.
        entt::entity target = entt::null;
        float nearest = 1e9f;
        auto stock = reg.view<ECS::Components::Resource,
                              ECS::Components::Transform>();
        for (auto item : stock) {
            if (reg.all_of<ECS::Components::Health>(item)) continue;
            const auto& rt = stock.get<ECS::Components::Transform>(item);
            const float dx = rt.position.x.ToFloat() - at.position.x.ToFloat();
            const float dy = rt.position.y.ToFloat() - at.position.y.ToFloat();
            const float d2 = dx * dx + dy * dy;
            if (d2 < nearest) { nearest = d2; target = item; }
        }

        if (target == entt::null) {
            verdict = "no resource on the map";
        } else {
            const int kind = static_cast<int>(
                reg.get<ECS::Components::Resource>(target).type);
            auto* owner = ECS::Players::Find(reg, Core::PLAYER_LOCAL);
            const int before =
                ECS::Players::Stock(*owner, static_cast<Core::ResourceType>(kind));
            const int stock_before =
                reg.get<ECS::Components::Resource>(target).current_amount;

            Core::CommandQueue::Get().PushCommand(
                Core::GatherCommand{gatherer, target, Core::PLAYER_LOCAL});

            bool ever_gathered = false;
            const char* animation_fault = nullptr;
            bool ever_carried = false;
            for (int t = 0; t < 4000; ++t) {
                headless.Update(16);
                if (!reg.valid(gatherer)) break;
                const auto& w = reg.get<ECS::Components::Worker>(gatherer);
                if (w.state == Core::UnitState::Gathering) {
                    ever_gathered = true;

                    // Taking from a resource has to look like work: a villager
                    // at a tree wears the lumberjack set and plays that set's
                    // work graphic, not its standing pose.
                    const auto& an = reg.get<ECS::Components::Animation>(gatherer);
                    const int work =
                        static_cast<int>(ECS::Components::AnimState::Work);
                    const auto& sp = reg.get<ECS::Components::Sprite>(gatherer);
                    if (an.job == ECS::Components::VillagerJob::Base) {
                        animation_fault = "gathering in the plain villager set";
                    } else if (an.slp[work] < 0 || an.frames[work] <= 0) {
                        animation_fault = "job set has no work animation";
                    } else if (an.slp[work] ==
                               an.slp[static_cast<int>(
                                   ECS::Components::AnimState::Idle)]) {
                        animation_fault = "work animation is the standing "
                                          "graphic";
                    } else if (!w.working) {
                        animation_fault = "gathering without being at work";
                    } else if (an.state != ECS::Components::AnimState::Work) {
                        animation_fault = "at work but not playing the work "
                                          "animation";
                    } else if (sp.slp_id != an.slp[work]) {
                        animation_fault = "work animation chosen but not on the "
                                          "sprite";
                    } else {
                        // And the interface agrees about what it is: a villager
                        // cutting wood shows as a Lumberjack, with the dat's
                        // own name for it, not as "Villager".
                        const int shown =
                            ECS::Villagers::DisplayUnit(reg, gatherer);
                        const int own =
                            reg.get<ECS::Components::Identity>(gatherer).unit_id;
                        const std::string& label =
                            Core::ConfigManager::Get().GetUnitName(shown);
                        if (shown == own) {
                            animation_fault = "interface still shows the plain "
                                              "villager while at work";
                        } else if (label.empty()) {
                            animation_fault = "job unit has no name to show";
                        }
                    }
                }
                if (w.carried[kind] > 0) {
                    ever_carried = true;

                    // Walking home with a load has its own graphic in the dat:
                    // a lumberjack shoulders the logs.
                    const auto* route = reg.try_get<ECS::Components::Path>(gatherer);
                    const bool walking =
                        route && route->current_waypoint_index <
                                     route->waypoints.size();
                    if (walking && !w.working && animation_fault == nullptr) {
                        const auto& an =
                            reg.get<ECS::Components::Animation>(gatherer);
                        const int carry =
                            static_cast<int>(ECS::Components::AnimState::Carry);
                        if (an.slp[carry] < 0 || an.frames[carry] <= 0) {
                            animation_fault = "job set has no carrying graphic";
                        } else if (an.state != ECS::Components::AnimState::Carry) {
                            animation_fault = "walking home loaded without the "
                                              "carrying graphic";
                        }
                    }
                }
                if (ECS::Players::Stock(*owner, static_cast<Core::ResourceType>(kind)) >
                    before) {
                    break;
                }
            }

            const int after =
                ECS::Players::Stock(*owner, static_cast<Core::ResourceType>(kind));
            const bool drained =
                !reg.valid(target) ||
                reg.get<ECS::Components::Resource>(target).current_amount <
                    stock_before;

            if (animation_fault != nullptr) {
                verdict = animation_fault;
            } else if (!ever_gathered) {
                verdict = "villager never reached the resource";
            } else if (!drained) {
                verdict = "gathered without taking from the resource";
            } else if (!ever_carried) {
                verdict = "took nothing away";
            } else if (after <= before) {
                verdict = "never delivered to a drop site";
            } else {
                verdict = "ok";
            }
        }
    }
    // Loads are independent, and a drop site takes only what it accepts.
    // Checked on the rules directly: a hand holds wood and gold at once, each as
    // itself, and a drop site takes what it accepts and leaves the rest in hand.
    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Components::Worker hand;
        hand.carried[static_cast<int>(Core::ResourceType::Wood)] = 10;
        hand.carried[static_cast<int>(Core::ResourceType::Gold)] = 5;

        const int town_centre = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
        const int mill = Core::ConfigManager::Get().GetUnitIdByName("MILL");
        const int lumber_camp = Core::ConfigManager::Get().GetUnitIdByName("SMIL");

        if (town_centre < 0 || mill < 0 || lumber_camp < 0) {
            verdict = "drop site units are missing from the data";
        } else if (ECS::DropSites::Accepts(mill, Core::ResourceType::Wood)) {
            verdict = "a mill claims to accept wood";
        } else if (!ECS::DropSites::Accepts(lumber_camp, Core::ResourceType::Wood)) {
            verdict = "a lumber camp refuses wood";
        } else if (!ECS::DropSites::Accepts(town_centre, Core::ResourceType::Wood) ||
                   !ECS::DropSites::Accepts(town_centre, Core::ResourceType::Gold)) {
            verdict = "the town centre refuses a resource";
        } else {
            // A lumber camp takes the wood and leaves the gold in hand.
            const int wood_slot = static_cast<int>(Core::ResourceType::Wood);
            const int gold_slot = static_cast<int>(Core::ResourceType::Gold);
            const int handed = ECS::DropSites::Deposit(lumber_camp, hand, nullptr);
            if (handed != 10) {
                verdict = "a lumber camp did not take the whole load of wood";
            } else if (hand.carried[wood_slot] != 0) {
                verdict = "wood stayed in hand after being accepted";
            } else if (hand.carried[gold_slot] != 5) {
                verdict = "a lumber camp swallowed gold it cannot accept";
            } else if (ECS::DropSites::Deposit(town_centre, hand, nullptr) != 5 ||
                       hand.carried[gold_slot] != 0) {
                verdict = "the town centre did not take the remaining gold";
            }
        }
    }

    // A farm is a building that becomes a resource, and a house is one that
    // provides population. Both are booked in the same storage field, so each
    // is read for what it is: a farm holds the civ's "Farm Food Amount"
    // (resource 36), 175, not the 15 its own storage slot states; a house's
    // storage is five of population headroom, not a gatherable.
    if (std::strcmp(verdict, "ok") == 0) {
        auto& cfg = Core::ConfigManager::Get();
        const int farm = cfg.GetUnitIdByName("FARM");
        const int house = cfg.GetUnitIdByName("HOUS");
        const int town_centre = cfg.GetUnitIdByName("RTWC");
        const auto* farm_ui = farm >= 0 ? cfg.GetUnitInterface(farm) : nullptr;
        const auto* house_ui = house >= 0 ? cfg.GetUnitInterface(house) : nullptr;
        const auto* tc_ui = town_centre >= 0 ? cfg.GetUnitInterface(town_centre)
                                             : nullptr;
        constexpr int kFarmFood = 175;
        constexpr int kHousingPerBuilding = 5;

        if (farm_ui == nullptr || house_ui == nullptr || tc_ui == nullptr) {
            verdict = "farm, house or town centre is missing from the data";
        } else if (farm_ui->stores_resource !=
                   static_cast<int>(Core::ResourceType::Food)) {
            verdict = "a farm holds no food";
        } else if (farm_ui->stores_amount != kFarmFood) {
            verdict = "a farm does not hold the civ's farm food amount";
        } else if (house_ui->stores_resource != -1) {
            verdict = "a house looks like a gatherable resource";
        } else if (house_ui->headroom != kHousingPerBuilding ||
                   tc_ui->headroom != kHousingPerBuilding) {
            verdict = "a house or town centre does not house five";
        }
    }

    // A farm's ground, through its life: the three construction stages while
    // it goes up, the planted field once it is finished -- holding the
    // player's own farm food amount -- and the harvested-out field when it is
    // exhausted, at which point the farm itself is gone.
    if (std::strcmp(verdict, "ok") == 0) {
        auto& cfg = Core::ConfigManager::Get();
        auto& grid = headless.GetGrid();
        const int farm_id = cfg.GetUnitIdByName("FARM");
        const auto& tc_at = reg.get<ECS::Components::Transform>(gatherer).position;
        const int fx = tc_at.x.ToInt() + 6;
        const int fy = tc_at.y.ToInt() - 6;
        const entt::entity field = ECS::Buildings::Place(
            reg, grid, farm_id, Core::PLAYER_LOCAL, fx, fy, false);
        auto ground_is = [&](Core::TerrainType wanted) {
            bool all = true;
            ECS::Buildings::ForEachFootprintTile(farm_id, fx, fy, [&](int x, int y) {
                if (grid.GetTile(x, y).terrain != wanted) all = false;
            });
            return all;
        };
        if (field == entt::null) {
            verdict = "a farm could not be placed";
        } else if (!ground_is(Core::TerrainType::FarmBuild1)) {
            verdict = "a farm site did not lay its first stage of farmland";
        } else {
            reg.get<ECS::Components::Building>(field).progress =
                Core::Fixed32(1) / Core::Fixed32(2);
            headless.Update(16);
            if (!ground_is(Core::TerrainType::FarmBuild2)) {
                verdict = "a half-built farm's ground did not move on a stage";
            } else {
                ECS::Buildings::Complete(reg, grid, field);
                const auto* owner = ECS::Players::Find(reg, Core::PLAYER_LOCAL);
                const int planted = owner != nullptr
                    ? ECS::Players::Resource(*owner,
                          Core::Rules::Res::FarmFoodAmount).ToInt() : -1;
                if (!ground_is(Core::TerrainType::Farm)) {
                    verdict = "a finished farm's crop did not come up";
                } else if (reg.get<ECS::Components::Resource>(field).current_amount !=
                           planted) {
                    verdict = "a farm does not hold the player's farm food amount";
                } else {
                    ECS::Resources::Exhaust(reg, grid, field);
                    if (reg.valid(field)) {
                        verdict = "an exhausted farm is still standing";
                    } else if (!ground_is(Core::TerrainType::FarmDead)) {
                        verdict = "an exhausted farm did not leave its field harvested";
                    } else {
                        // Its dead farm, the owner's, to be rebuilt: a new
                        // Farm on the same ground takes its place.
                        const int dead_id = Core::Rules::Unit(farm_id)->dead_unit;
                        auto dead_at = [&]() {
                            int n = 0;
                            auto remains = reg.view<ECS::Components::Identity, ECS::Components::Owner,
                                                    ECS::Components::Transform>();
                            for (auto e : remains) {
                                if (remains.get<ECS::Components::Identity>(e).unit_id != dead_id) continue;
                                if (remains.get<ECS::Components::Owner>(e).player_id != Core::PLAYER_LOCAL) continue;
                                const auto& p = remains.get<ECS::Components::Transform>(e).position;
                                if (p.x.ToInt() == fx && p.y.ToInt() == fy) ++n;
                            }
                            return n;
                        };
                        if (dead_at() != 1) {
                            verdict = "an exhausted farm left no dead farm of its owner's to rebuild";
                        } else {
                            const entt::entity again = ECS::Buildings::Place(
                                reg, grid, farm_id, Core::PLAYER_LOCAL, fx, fy, false);
                            if (again == entt::null) {
                                verdict = "a farm could not be rebuilt on its dead farm";
                            } else if (dead_at() != 0) {
                                verdict = "a farm rebuilt on its dead farm left the dead farm";
                            } else {
                                ECS::Buildings::Destroy(reg, grid, again, false);
                            }
                        }
                    }
                }
            }
        }
    }

    // A felled tree frees its ground.
    if (std::strcmp(verdict, "ok") == 0) {
        auto& grid = headless.GetGrid();
        entt::entity tree = entt::null;
        auto stock = reg.view<ECS::Components::Resource, ECS::Components::Transform>();
        for (auto item : stock) {
            if (stock.get<ECS::Components::Resource>(item).type != Core::ResourceType::Wood) continue;
            const auto& at = stock.get<ECS::Components::Transform>(item).position;
            if (grid.IsWalkable(at.x.ToInt(), at.y.ToInt())) continue;
            tree = item;
            break;
        }
        if (tree == entt::null) {
            verdict = "no tree blocking its tile to fell";
        } else {
            const auto at = reg.get<ECS::Components::Transform>(tree).position;
            ECS::Resources::Exhaust(reg, grid, tree);
            if (!grid.IsWalkable(at.x.ToInt(), at.y.ToInt())) {
                verdict = "a felled tree still blocks its tile";
            }
        }
    }

    // Every selectable map object names itself and says what is left in it,
    // Gaia's included, though Gaia units live outside the playable civ's table.
    if (std::strcmp(verdict, "ok") == 0) {
        auto& cfg = Core::ConfigManager::Get();
        int examined = 0;
        int living_seen = 0;
        auto objects = reg.view<ECS::Components::Resource,
                                ECS::Components::Identity,
                                ECS::Components::Selectable>();
        for (auto object : objects) {
            const int unit_id = objects.get<ECS::Components::Identity>(object).unit_id;
            const auto* ui = cfg.GetUnitInterface(unit_id);
            if (ui == nullptr || ui->name.empty()) {
                verdict = "a map object has no name to show";
                break;
            }
            if (objects.get<ECS::Components::Resource>(object).current_amount <= 0) {
                verdict = "a map object holds nothing";
                break;
            }
            // Living things have hit points to show and static ones do not, by
            // the dat's own unit type.
            constexpr int kLivingUnitType = 70;
            const bool living = ui->unit_type == kLivingUnitType && ui->hp > 0;
            if (living) ++living_seen;
            if (living != reg.all_of<ECS::Components::Health>(object)) {
                verdict = living ? "a living map object has no hit points"
                                 : "a static map object was given hit points";
                break;
            }
            // Scenery is pointed at, never swept up by a selection box.
            if (objects.get<ECS::Components::Selectable>(object).priority != 0) {
                verdict = "a map object would be caught by a selection box";
                break;
            }
            ++examined;
        }
        if (std::strcmp(verdict, "ok") == 0 && examined == 0) {
            verdict = "no map object is selectable at all";
        }
        // Every shipped map starts each player with sheep, so finding no living
        // map object means the unit type was lost.
        if (std::strcmp(verdict, "ok") == 0 && living_seen == 0) {
            verdict = "no living map object was found";
        }

        // And the interface is told about it: the panel's own query describes a
        // clicked map object the player does not own.
        if (std::strcmp(verdict, "ok") == 0) {
            constexpr Core::PlayerId kPlayer = 1;
            entt::entity tree = entt::null;
            for (auto object : objects) { tree = object; break; }

            auto everything = reg.view<ECS::Components::Selectable>();
            for (auto e : everything) {
                everything.get<ECS::Components::Selectable>(e).is_selected = (e == tree);
            }

            if (ECS::Selection::Inspected(reg, kPlayer) != tree) {
                verdict = "a clicked map object is not what the panel describes";
            } else if (ECS::Selection::Commanded(reg, kPlayer) != entt::null) {
                // Nor may it be handed a command grid or a delete button.
                verdict = "a map object can be given orders";
            }
            for (auto e : everything) {
                everything.get<ECS::Components::Selectable>(e).is_selected = false;
            }
        }
    }

    // A farm: a building that is a resource. Its ground stays walkable, so a
    // farmer works it standing on it -- and has to get there, take food from
    // it and carry it home like any other gatherer.
    if (std::strcmp(verdict, "ok") == 0) {
        auto& grid = headless.GetGrid();
        const int farm_id = Core::ConfigManager::Get().GetUnitIdByName("FARM");
        entt::entity farmer = entt::null;
        ECS::Components::Vector2D home{};
        auto mine = reg.view<ECS::Components::Worker, ECS::Components::Transform,
                             ECS::Components::Owner>();
        for (auto v : mine) {
            if (mine.get<ECS::Components::Owner>(v).player_id != Core::PLAYER_LOCAL) continue;
            if (v == gatherer) continue;
            farmer = v;
            home = mine.get<ECS::Components::Transform>(v).position;
            break;
        }
        entt::entity farm = entt::null;
        for (int r = 2; r < 12 && farm == entt::null; ++r) {
            for (int d = -r; d <= r && farm == entt::null; ++d) {
                const int x = Physics::TileOf(home.x) + d;
                const int y = Physics::TileOf(home.y) + r;
                if (ECS::Buildings::CanPlace(grid, farm_id, x, y)) {
                    farm = ECS::Buildings::Place(reg, grid, farm_id, Core::PLAYER_LOCAL, x, y, true);
                }
            }
        }
        if (farmer == entt::null || farm == entt::null) {
            verdict = "no villager or ground to try a farm";
        } else {
            auto* owner = ECS::Players::Find(reg, Core::PLAYER_LOCAL);
            const int food = ECS::Players::Stock(*owner, Core::ResourceType::Food);
            const int held = reg.get<ECS::Components::Resource>(farm).current_amount;
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{farmer, farm, Core::PLAYER_LOCAL});
            bool farmed = false;
            for (int t = 0; t < 90 * 1000 / 16; ++t) {
                headless.Update(16);
                const auto& w = reg.get<ECS::Components::Worker>(farmer);
                if (w.state == Core::UnitState::Gathering) farmed = true;
                if (ECS::Players::Stock(*owner, Core::ResourceType::Food) > food) break;
            }
            if (!farmed) {
                verdict = "a farmer never started work on a farm";
            } else if (!reg.valid(farm) || reg.get<ECS::Components::Resource>(farm).current_amount >= held) {
                verdict = "a farmer took nothing from the farm";
            } else if (ECS::Players::Stock(*owner, Core::ResourceType::Food) <= food) {
                verdict = "a farmer never brought food home";
            }
        }
    }

    // A tree cut down: the lumberjack goes on to the next tree of the forest,
    // within its search radius of the one that fell, and never stands idle in
    // between -- the dat's gather tasks search for another target.
    if (std::strcmp(verdict, "ok") == 0) {
        auto& grid = headless.GetGrid();
        entt::entity tree = entt::null;
        int vx = 0, vy = 0;
        auto stock = reg.view<ECS::Components::Resource, ECS::Components::Transform>();
        for (auto item : stock) {
            if (tree != entt::null) break;
            if (stock.get<ECS::Components::Resource>(item).type != Core::ResourceType::Wood) continue;
            const auto& at = stock.get<ECS::Components::Transform>(item).position;
            const int tx = Physics::TileOf(at.x), ty = Physics::TileOf(at.y);
            // In a forest -- another tree beside it -- and with open ground
            // beside it to stand on.
            int neighbours = 0;
            bool open = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (!grid.InBounds(tx + dx, ty + dy)) continue;
                    if (grid.GetTile(tx + dx, ty + dy).has_tree) ++neighbours;
                    if (!open && grid.IsWalkable(tx + dx, ty + dy)) {
                        open = true;
                        vx = tx + dx;
                        vy = ty + dy;
                    }
                }
            }
            if (neighbours >= 2 && open) tree = item;
        }
        entt::entity cutter = entt::null;
        auto mine = reg.view<ECS::Components::Worker, ECS::Components::Transform,
                             ECS::Components::Owner>();
        for (auto v : mine) {
            if (mine.get<ECS::Components::Owner>(v).player_id == Core::PLAYER_LOCAL &&
                mine.get<ECS::Components::Worker>(v).state == Core::UnitState::Idle) {
                cutter = v;
            }
        }
        if (tree == entt::null || cutter == entt::null) {
            verdict = "no forest tree or idle villager to try the next tree";
        } else {
            const ECS::Components::Vector2D fell = reg.get<ECS::Components::Transform>(tree).position;
            reg.get<ECS::Components::Resource>(tree).current_amount = 2;
            auto& stand = reg.get<ECS::Components::Transform>(cutter);
            stand.position = stand.previous_position = Shore::At(vx, vy);
            reg.remove<ECS::Components::Path>(cutter);
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{cutter, tree, Core::PLAYER_LOCAL});
            entt::entity next = entt::null;
            bool idle = false;
            bool cutting = false;
            for (int t = 0; t < 60 * 1000 / 16 && !cutting && !idle; ++t) {
                headless.Update(16);
                const auto& w = reg.get<ECS::Components::Worker>(cutter);
                if (!reg.valid(tree) || reg.get<ECS::Components::Resource>(tree).current_amount <= 0) {
                    if (w.state == Core::UnitState::Idle) idle = true;
                    if (reg.valid(w.target_resource) && w.target_resource != tree) next = w.target_resource;
                    cutting = next != entt::null && w.target_resource == next &&
                              w.state == Core::UnitState::Gathering;
                }
            }
            const auto& w = reg.get<ECS::Components::Worker>(cutter);
            if (reg.valid(tree) && reg.get<ECS::Components::Resource>(tree).current_amount > 0) {
                verdict = "a lumberjack never cut down its tree";
            } else if (idle || next == entt::null) {
                verdict = "a lumberjack stood idle when its tree fell, with the forest beside it";
            } else if (reg.get<ECS::Components::Resource>(next).type != Core::ResourceType::Wood ||
                       w.job != ECS::Components::VillagerJob::Lumber) {
                verdict = "a lumberjack went on to something other than a tree";
            } else if (Core::FixedMath::Distance(reg.get<ECS::Components::Transform>(next).position.x - fell.x,
                                                 reg.get<ECS::Components::Transform>(next).position.y - fell.y) >
                       Core::Fixed32(4)) {
                verdict = "a lumberjack went on to a tree beyond its search radius";
            } else if (!cutting) {
                verdict = "a lumberjack never started on the next tree";
            }
        }
    }

    // A farm takes one farmer: two sent at one farm, the second goes to the
    // free farm beside it. A farm's builder farms it; a Lumber Camp's builder
    // cuts the forest beside it. A world of its own.
    if (std::strcmp(verdict, "ok") == 0) {
        ECS::Simulation town;
        town.Init();
        auto& treg = town.GetRegistry();
        auto& tgrid = town.GetGrid();
        auto& cfg = Core::ConfigManager::Get();
        const int farm_id = cfg.GetUnitIdByName("FARM");
        const int camp_id = cfg.GetUnitIdByName("SMIL");
        std::vector<entt::entity> crew;
        auto mine = treg.view<ECS::Components::Worker, ECS::Components::Owner>();
        for (auto v : mine) {
            if (mine.get<ECS::Components::Owner>(v).player_id == Core::PLAYER_LOCAL) crew.push_back(v);
        }
        std::sort(crew.begin(), crew.end());
        const auto [sx, sy] = town.player_starts[0];
        // Two farms side by side, finished, where there is room.
        entt::entity a = entt::null, b = entt::null;
        int fx = 0, fy = 0;
        for (int r = 4; r < 14 && b == entt::null; ++r) {
            for (int d = -r; d <= r && b == entt::null; ++d) {
                const int x = sx + d, y = sy + r;
                if (ECS::Buildings::CanPlace(tgrid, farm_id, x, y) &&
                    ECS::Buildings::CanPlace(tgrid, farm_id, x + 3, y)) {
                    a = ECS::Buildings::Place(treg, tgrid, farm_id, Core::PLAYER_LOCAL, x, y, true);
                    b = ECS::Buildings::Place(treg, tgrid, farm_id, Core::PLAYER_LOCAL, x + 3, y, true);
                    fx = x;
                    fy = y;
                }
            }
        }
        if (crew.size() < 3 || a == entt::null || b == entt::null) {
            verdict = "no villagers or ground for the farms";
        } else {
            for (int i = 0; i < 2; ++i) {
                Core::CommandQueue::Get().PushCommand(Core::GatherCommand{crew[i], a, Core::PLAYER_LOCAL});
            }
            for (int t = 0; t < 60 * 1000 / 16; ++t) town.Update(16);
            int on_a = 0, on_b = 0;
            for (int i = 0; i < 2; ++i) {
                const auto& w = treg.get<ECS::Components::Worker>(crew[i]);
                on_a += w.target_resource == a ? 1 : 0;
                on_b += w.target_resource == b ? 1 : 0;
            }
            if (on_a != 1 || on_b != 1) {
                verdict = "two villagers sent at one farm did not end on two farms";
            }
        }
        // A farm's builder farms it.
        if (std::strcmp(verdict, "ok") == 0) {
            int px = 0, py = 0;
            bool room = false;
            for (int r = 4; r < 16 && !room; ++r) {
                for (int d = -r; d <= r && !room; ++d) {
                    if (ECS::Buildings::CanPlace(tgrid, farm_id, fx + d, fy - r)) {
                        px = fx + d;
                        py = fy - r;
                        room = true;
                    }
                }
            }
            if (auto* stock = ECS::Players::Find(treg, Core::PLAYER_LOCAL)) {
                ECS::Players::AddStock(*stock, Core::ResourceType::Wood, 500);
            }
            // Farms come with a Mill.
            const int mill_id = cfg.GetUnitIdByName("MILL");
            bool milled = false;
            for (int r = 6; r < 20 && !milled; ++r) {
                for (int d = -r; d <= r && !milled; ++d) {
                    if (ECS::Buildings::CanPlace(tgrid, mill_id, sx - r, sy + d)) {
                        const entt::entity mill =
                            ECS::Buildings::Place(treg, tgrid, mill_id, Core::PLAYER_LOCAL, sx - r, sy + d, false);
                        ECS::Buildings::Complete(treg, tgrid, mill);
                        milled = true;
                    }
                }
            }
            Core::CommandQueue::Get().PushCommand(Core::BuildCommand{crew[2], farm_id, px, py, Core::PLAYER_LOCAL});
            bool farming = false;
            for (int t = 0; t < 120 * 1000 / 16 && !farming; ++t) {
                town.Update(16);
                const auto& w = treg.get<ECS::Components::Worker>(crew[2]);
                farming = w.state == Core::UnitState::Gathering && treg.valid(w.target_resource) &&
                          treg.all_of<ECS::Components::Building>(w.target_resource) &&
                          w.job == ECS::Components::VillagerJob::Farm;
            }
            if (!room) verdict = "no ground for a farm to build";
            else if (!farming) verdict = "a farm's builder did not farm it";
        }
        // A Lumber Camp's builder cuts the forest beside it.
        if (std::strcmp(verdict, "ok") == 0) {
            int cx = 0, cy = 0;
            bool room = false;
            // The forest nearest the town, so the builder knows it and the
            // walk is short.
            std::vector<std::pair<int, entt::entity>> by_distance;
            auto trees = treg.view<ECS::Components::Resource, ECS::Components::Transform>();
            for (auto e : trees) {
                if (trees.get<ECS::Components::Resource>(e).type != Core::ResourceType::Wood) continue;
                const auto& at = trees.get<ECS::Components::Transform>(e).position;
                const int dx = Physics::TileOf(at.x) - sx, dy = Physics::TileOf(at.y) - sy;
                by_distance.push_back({dx * dx + dy * dy, e});
            }
            std::sort(by_distance.begin(), by_distance.end());
            for (const auto& [far, e] : by_distance) {
                if (room) break;
                if (far < 36) continue;
                const auto& at = trees.get<ECS::Components::Transform>(e).position;
                const int tx = Physics::TileOf(at.x), ty = Physics::TileOf(at.y);
                for (int dy = -3; dy <= 3 && !room; ++dy) {
                    for (int dx = -3; dx <= 3 && !room; ++dx) {
                        if (std::abs(dx) + std::abs(dy) < 2) continue;
                        if (ECS::Buildings::CanPlace(tgrid, camp_id, tx + dx, ty + dy)) {
                            cx = tx + dx;
                            cy = ty + dy;
                            room = true;
                        }
                    }
                }
            }
            const entt::entity builder = crew.size() > 3 ? crew[3] : crew[0];
            Core::CommandQueue::Get().PushCommand(Core::BuildCommand{builder, camp_id, cx, cy, Core::PLAYER_LOCAL});
            bool cutting = false;
            for (int t = 0; t < 240 * 1000 / 16 && !cutting; ++t) {
                town.Update(16);
                const auto& w = treg.get<ECS::Components::Worker>(builder);
                cutting = treg.valid(w.target_resource) && w.job == ECS::Components::VillagerJob::Lumber &&
                          (w.state == Core::UnitState::Gathering || w.state == Core::UnitState::Moving);
            }
            if (!room) verdict = "no ground beside a forest for a Lumber Camp";
            else if (!cutting) verdict = "a Lumber Camp's builder did not cut the forest beside it";
        }
    }

    std::printf("gathering=%s\n", verdict);
    return std::strcmp(verdict, "ok") == 0;
}

} // namespace Verify
