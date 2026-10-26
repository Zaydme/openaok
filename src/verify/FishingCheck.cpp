#include "verify/Harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ai/Census.h"
#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Navigation.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Vector2D;
namespace R = Core::Rules;

constexpr Core::PlayerId kPlayer = 1;
constexpr int kFood = static_cast<int>(Core::ResourceType::Food);

// The dat's class of fish in the open sea.
constexpr int kFishClass = 5;

// The newest unit of `unit_id` the player has, by membership, not view order.
entt::entity NewUnit(const entt::registry& reg, int unit_id, const std::vector<entt::entity>& before) {
    auto all = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : all) {
        if (all.get<const ECS::Components::Identity>(e).unit_id != unit_id) continue;
        if (all.get<const ECS::Components::Owner>(e).player_id != kPlayer) continue;
        bool seen = false;
        for (auto b : before) seen = seen || b == e;
        if (!seen) return e;
    }
    return entt::null;
}

std::vector<entt::entity> UnitsOf(const entt::registry& reg, int unit_id) {
    std::vector<entt::entity> out;
    auto all = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : all) {
        if (all.get<const ECS::Components::Identity>(e).unit_id == unit_id) out.push_back(e);
    }
    return out;
}

// Gathers until the ship turns for home with a full load or `limit` ticks
// pass. Returns how many ticks it spent at work, -1 if it never started.
int WorkUntilLoaded(ECS::Simulation& sim, entt::entity ship, int limit) {
    auto& reg = sim.GetRegistry();
    int working = -1;
    for (int t = 0; t < limit; ++t) {
        sim.Update(16);
        if (!reg.valid(ship) || !reg.all_of<ECS::Components::Worker>(ship)) return -1;
        const auto& w = reg.get<ECS::Components::Worker>(ship);
        if (w.state == Core::UnitState::Gathering) working = std::max(0, working) + 1;
        if (working >= 0 && w.state == Core::UnitState::Returning) return working;
    }
    return working;
}

// Whether a measured spell of work matches what the dat's rate says, within
// a tenth: `ticks` at work for `amount` at `per_second`.
bool AtRate(int ticks, int amount, Fixed32 per_second) {
    const Fixed32 expected = Fixed32(amount) / per_second * Fixed32(1000) / Fixed32(16);
    const Fixed32 got(ticks);
    return got * Fixed32(10) >= expected * Fixed32(9) && got * Fixed32(10) <= expected * Fixed32(11);
}

} // namespace

// A fishing ship's whole round trip, on Islands: trained at a Dock built on
// the shore by the dat's placement rules, it is launched on the water side,
// sails to the fish, takes a load at the dat's rate (its work rate times its
// gather task's value for open-sea fish, 0.28 x 1.75), carries the dat's
// fifteen home to the Dock and hands it over as food. Then a fish trap: built
// by a fishing ship on the water -- and refused to a villager, as a house is
// refused to the ship -- it holds the civ's Fish Trap Food Amount (700) and
// is worked at the trap's own value (0.75).
bool CheckFishing() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int dock_id = cfg.GetUnitIdByName("DOCK");
    const int ship_id = cfg.GetUnitIdByName("FSHSP");
    const int trap_id = cfg.GetUnitIdByName("FTRAP");
    const int house_id = cfg.GetUnitIdByName("HOUS");
    const R::UnitRules* ship_rules = R::Unit(ship_id);
    if (dock_id < 0 || ship_id < 0 || trap_id < 0 || ship_rules == nullptr) {
        std::printf("fishing=the dat has no dock, fishing ship or fish trap\n");
        return false;
    }

    // Feudal, so the fish trap is there to build.
    Core::GameSetup setup = Core::DefaultSetup();
    setup.map_script = "Islands";
    setup.starting_age = 1;
    ECS::Simulation sim;
    sim.Init(setup);
    auto& reg = sim.GetRegistry();
    auto& grid = sim.GetGrid();
    const Physics::Layer sea = Shore::Sea(sim);
    const auto [sx, sy] = sim.player_starts[0];

    // The Dock, where the dat lets one stand: somewhere on water or
    // shallows, against a beach -- one whose front, where a land unit would
    // step out, is dry land, so a ship launched as though it were one shows.
    entt::entity dock = entt::null;
    for (int r = 0; r <= 40 && dock == entt::null; ++r) {
        for (int dy = -r; dy <= r && dock == entt::null; ++dy) {
            for (int dx = -r; dx <= r && dock == entt::null; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int x = sx + dx, y = sy + dy;
                if (!ECS::Buildings::CanPlace(grid, dock_id, x, y)) continue;
                if (!grid.IsWalkable(x + 2, y + 2) || grid.Passable(sea, x + 2, y + 2)) continue;
                dock = ECS::Buildings::Place(reg, grid, dock_id, kPlayer, x, y, true);
            }
        }
    }
    if (dock == entt::null) verdict = "no shore on the first island takes a dock";
    if (ok()) {
        // Not out at sea, and not up on the land: the dat's own rule, checked
        // on open water -- the shallow-going WATER a dock stands on, with no
        // beach anywhere round it -- and on a tile inland.
        const int inland_x = sx + 4, inland_y = sy + 4;
        const int water = 1;   // the dat's WATER terrain
        bool far_out = false;
        for (int y = 2; y < grid.GetHeight() - 2 && !far_out; ++y) {
            for (int x = 2; x < grid.GetWidth() - 2 && !far_out; ++x) {
                bool open = true;
                bool shallow = false;
                for (int oy = -2; oy <= 2 && open; ++oy) {
                    for (int ox = -2; ox <= 2 && open; ++ox) {
                        open = grid.Passable(sea, x + ox, y + oy) && !grid.IsWalkable(x + ox, y + oy);
                        // Under the footprint itself (three tiles across).
                        shallow = shallow || (std::abs(ox) <= 1 && std::abs(oy) <= 1 &&
                                              cfg.GetTerrainArt(grid.GetTile(x + ox, y + oy).terrain)
                                                      .dat_id == water);
                    }
                }
                if (open && shallow) {
                    far_out = true;
                    if (ECS::Buildings::CanPlace(grid, dock_id, x, y)) {
                        verdict = "a dock may be placed out at sea";
                    }
                }
            }
        }
        if (ok() && !far_out) verdict = "Islands has no open water to try a dock on";
        if (ok() && ECS::Buildings::CanPlace(grid, dock_id, inland_x, inland_y)) {
            verdict = "a dock may be placed on dry land";
        }
    }

    // A fishing ship trained there, for the dat's price, launched on the water.
    entt::entity ship = entt::null;
    auto* me = ECS::Players::Find(reg, kPlayer);
    if (ok()) {
        const int wood = ECS::Players::Stock(*me, Core::ResourceType::Wood);
        const auto before = UnitsOf(reg, ship_id);
        Core::CommandQueue::Get().PushCommand(Core::TrainCommand{dock, ship_id, kPlayer});
        for (int t = 0; t < (ship_rules->train_time + 5) * Shore::kTicksPerSecond; ++t) {
            sim.Update(16);
            ship = NewUnit(reg, ship_id, before);
            if (ship != entt::null) break;
        }
        int price = 0;
        for (int c = 0; c < ship_rules->cost_count; ++c) {
            if (ship_rules->costs[c].resource == R::Res::Wood && ship_rules->costs[c].paid) {
                price += ship_rules->costs[c].amount;
            }
        }
        if (ship == entt::null) {
            verdict = "the dock never launched its fishing ship";
        } else if (ECS::Players::Stock(*me, Core::ResourceType::Wood) != wood - price) {
            verdict = "the fishing ship was not charged the dat's price";
        } else {
            const Vector2D at = reg.get<ECS::Components::Transform>(ship).position;
            if (!grid.Passable(sea, Physics::TileOf(at.x), Physics::TileOf(at.y))) {
                verdict = "the fishing ship was launched onto the land";
            } else if (!reg.all_of<ECS::Components::Worker>(ship)) {
                verdict = "a fishing ship cannot work";
            }
        }
    }

    // It will not go after a tree.
    if (ok()) {
        constexpr int kTreeClass = 15;
        const Vector2D at = reg.get<ECS::Components::Transform>(ship).position;
        entt::entity tree = entt::null;
        Fixed32 nearest(1 << 20);
        auto wood = reg.view<const ECS::Components::Identity, const ECS::Components::Transform,
                             const ECS::Components::Resource>();
        for (auto e : wood) {
            const auto* rules = R::Unit(wood.get<const ECS::Components::Identity>(e).unit_id);
            if (rules == nullptr || rules->cls != kTreeClass) continue;
            const Fixed32 d = Shore::Apart(wood.get<const ECS::Components::Transform>(e).position, at);
            if (d < nearest) { nearest = d; tree = e; }
        }
        if (tree != entt::null) {
            // Judged on the order itself, not a tick later: an unreachable
            // tree would be given up by then whether or not it was refused.
            ECS::Orders::Execute(reg, grid, Core::GatherCommand{ship, tree, kPlayer});
            const auto& w = reg.get<ECS::Components::Worker>(ship);
            if (w.state != Core::UnitState::Idle || w.target_resource == tree) {
                verdict = "a fishing ship was sent to fell a tree";
            }
        }
    }

    // The round trip: to the fish, a load at the dat's rate, home to the dock.
    if (ok()) {
        const entt::entity fish = Shore::NearestOfClass(
            sim, ship, kFishClass, reg.get<ECS::Components::Transform>(ship).position);
        if (fish == entt::null) {
            verdict = "no fish the fishing ship can reach";
        } else if (!reg.all_of<ECS::Components::Resource>(fish) ||
                   reg.get<ECS::Components::Resource>(fish).type != Core::ResourceType::Food) {
            // The dat stores fish as "fish" (17), which the gather task hands
            // over as food: the map must have made that food.
            verdict = "the fish hold no food";
        } else {
            const int stock = reg.get<ECS::Components::Resource>(fish).current_amount;
            const int food = ECS::Players::Stock(*me, Core::ResourceType::Food);
            Core::CommandQueue::Get().PushCommand(Core::GatherCommand{ship, fish, kPlayer});
            const int worked = WorkUntilLoaded(sim, ship, 120 * Shore::kTicksPerSecond);
            const auto& w = reg.get<ECS::Components::Worker>(ship);
            Fixed32 value(1);
            for (int t = 0; t < ship_rules->task_count; ++t) {
                if (ship_rules->tasks[t].action == R::TaskAction::Gather &&
                    ship_rules->tasks[t].Targets(-1, kFishClass)) {
                    value = ship_rules->tasks[t].work_value;
                }
            }
            if (worked < 0) {
                verdict = "the fishing ship never reached the fish";
            } else if (w.carried[kFood] != ship_rules->carry) {
                verdict = "the fishing ship did not carry the dat's load";
            } else if (!reg.valid(fish) ||
                       reg.get<ECS::Components::Resource>(fish).current_amount != stock - ship_rules->carry) {
                verdict = "the catch did not come out of the fish";
            } else if (!AtRate(worked, ship_rules->carry, ship_rules->work_rate * value)) {
                verdict = "the fishing ship did not fish at the dat's rate";
            }
            // Home, and handed over as food at the Dock.
            bool delivered = false;
            for (int t = 0; t < 120 * Shore::kTicksPerSecond && ok(); ++t) {
                sim.Update(16);
                const Vector2D at = reg.get<ECS::Components::Transform>(ship).position;
                if (!grid.Passable(sea, Physics::TileOf(at.x), Physics::TileOf(at.y))) {
                    verdict = "the fishing ship sailed onto the land";
                }
                if (ECS::Players::Stock(*me, Core::ResourceType::Food) >= food + ship_rules->carry) {
                    delivered = true;
                    break;
                }
            }
            if (ok() && !delivered) verdict = "the catch was never landed at the dock";
            if (ok() && reg.get<ECS::Components::Worker>(ship).state != Core::UnitState::Moving &&
                reg.get<ECS::Components::Worker>(ship).state != Core::UnitState::Gathering) {
                verdict = "the fishing ship did not go back to the fish";
            }
        }
    }

    // A fish trap: the ship builds it on the water; a villager may not, nor
    // may the ship build a house.
    if (ok()) {
        const Vector2D dock_at = reg.get<ECS::Components::Transform>(dock).position;
        int tx = 0, ty = 0;
        if (!Shore::NearestSpot(grid, trap_id, Physics::TileOf(dock_at.x) + 4,
                                Physics::TileOf(dock_at.y) + 4, 12, tx, ty)) {
            verdict = "no water near the dock takes a fish trap";
        } else if (!grid.Passable(sea, tx, ty)) {
            verdict = "a fish trap was placed where no ship can sail";
        }
        entt::entity villager = entt::null;
        auto workers = reg.view<ECS::Components::Worker, ECS::Components::Owner,
                                ECS::Components::Identity>();
        for (auto v : workers) {
            if (workers.get<ECS::Components::Owner>(v).player_id == kPlayer &&
                workers.get<ECS::Components::Worker>(v).gatherer < 0) {
                villager = v;
            }
        }
        const int wood = ECS::Players::Stock(*me, Core::ResourceType::Wood);
        const size_t traps = UnitsOf(reg, trap_id).size();
        if (ok() && villager != entt::null) {
            Core::CommandQueue::Get().PushCommand(Core::BuildCommand{villager, trap_id, tx, ty, kPlayer});
            sim.Update(16);
            if (UnitsOf(reg, trap_id).size() != traps ||
                ECS::Players::Stock(*me, Core::ResourceType::Wood) != wood) {
                verdict = "a villager was let build a fish trap";
            }
        }
        if (ok()) {
            const int hx = Physics::TileOf(reg.get<ECS::Components::Transform>(dock).position.x);
            const int hy = Physics::TileOf(reg.get<ECS::Components::Transform>(dock).position.y);
            int px = 0, py = 0;
            if (Shore::NearestSpot(grid, house_id, hx, hy, 20, px, py)) {
                const size_t houses = UnitsOf(reg, house_id).size();
                Core::CommandQueue::Get().PushCommand(Core::BuildCommand{ship, house_id, px, py, kPlayer});
                sim.Update(16);
                if (UnitsOf(reg, house_id).size() != houses) verdict = "a fishing ship was let build a house";
            }
        }
        if (ok()) {
            Core::CommandQueue::Get().PushCommand(Core::BuildCommand{ship, trap_id, tx, ty, kPlayer});
            entt::entity trap = entt::null;
            bool finished = false;
            for (int t = 0; t < 90 * Shore::kTicksPerSecond && !finished; ++t) {
                sim.Update(16);
                for (auto e : UnitsOf(reg, trap_id)) trap = e;
                if (trap != entt::null && reg.all_of<ECS::Components::Building>(trap) &&
                    reg.get<ECS::Components::Building>(trap).state ==
                        ECS::Components::BuildingState::Complete) {
                    finished = true;
                }
            }
            if (trap == entt::null) {
                verdict = "the fishing ship never laid a fish trap";
            } else if (!finished) {
                verdict = "the fish trap was never finished";
            } else if (!reg.all_of<ECS::Components::Resource>(trap) ||
                       reg.get<ECS::Components::Resource>(trap).current_amount !=
                           ECS::Players::Resource(*me, R::Res::FishTrapFoodAmount).ToInt()) {
                verdict = "the fish trap does not hold the civ's fish trap food";
            } else if (ECS::Players::Resource(*me, R::Res::FishTrapFoodAmount).ToInt() <= 0) {
                verdict = "the civ has no fish trap food";
            } else {
                const auto& at = reg.get<ECS::Components::Transform>(trap).position;
                if (grid.GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).terrain ==
                    Core::TerrainType::FarmBuild1) {
                    verdict = "the fish trap laid farmland on the water";
                }
            }
            // Worked at the trap's own value.
            if (ok()) {
                auto& w = reg.get<ECS::Components::Worker>(ship);
                w.carried[kFood] = 0;
                w.gather_progress = Fixed32(0);
                Core::CommandQueue::Get().PushCommand(Core::GatherCommand{ship, trap, kPlayer});
                const int worked = WorkUntilLoaded(sim, ship, 150 * Shore::kTicksPerSecond);
                Fixed32 value(1);
                for (int t = 0; t < ship_rules->task_count; ++t) {
                    if (ship_rules->tasks[t].action == R::TaskAction::Gather &&
                        ship_rules->tasks[t].Targets(trap_id, R::Unit(trap_id)->cls)) {
                        value = ship_rules->tasks[t].work_value;
                    }
                }
                if (worked < 0) {
                    verdict = "the fishing ship never worked its fish trap";
                } else if (!AtRate(worked, ship_rules->carry, ship_rules->work_rate * value)) {
                    verdict = "the fish trap was not worked at the dat's rate";
                }
            }
        }
    }

    // The computer on Islands, by the "petersen fishboat" rules its scripts
    // load on the water maps: a Dock once a lumber camp is up, fishing ships
    // trained at it in the Dark Age, and their catch landed -- an island town's
    // food from the sea. Played until both players have landed a catch, or for
    // at most 25 minutes.
    if (ok()) {
        Core::GameSetup game = Core::DefaultSetup();
        game.map_script = "Islands";
        game.players[1].computer = true;
        game.players[2].computer = true;
        ECS::Simulation island;
        island.Init(game);
        auto& ireg = island.GetRegistry();
        // And its twin: every machine in a lockstep game runs its own copy of
        // the computer players, fleet and all, and they must agree.
        ECS::Simulation twin;
        twin.Init(game);
        bool diverged = false;
        const bool trace = std::getenv("AOE_AI_TRACE") != nullptr;
        constexpr int kMinutes = 25;
        bool docked[3] = {false, false, false};
        bool fished[3] = {false, false, false};
        int landed[3] = {0, 0, 0};
        int carried_before[3][64] = {};
        for (int minute = 1; minute <= kMinutes; ++minute) {
            if (docked[1] && docked[2] && landed[1] > 0 && landed[2] > 0) break;
            for (int t = 0; t < 60 * Shore::kTicksPerSecond; ++t) {
                island.Update(16);
                twin.Update(16);
                // A load landed: a fishing ship that was carrying has emptied
                // its hold.
                int slot[3] = {0, 0, 0};
                auto ships = ireg.view<const ECS::Components::Worker, const ECS::Components::Owner,
                                       const ECS::Components::Identity>();
                for (auto e : ships) {
                    const auto& w = ships.get<const ECS::Components::Worker>(e);
                    const Core::PlayerId p = ships.get<const ECS::Components::Owner>(e).player_id;
                    if (w.gatherer < 0 || p < 1 || p > 2) continue;
                    if (w.state == Core::UnitState::Gathering) fished[p] = true;
                    int& before = carried_before[p][slot[p]++ % 64];
                    if (before > 0 && w.carried[kFood] == 0 && w.state != Core::UnitState::Idle) {
                        ++landed[p];
                    }
                    before = w.carried[kFood];
                }
            }
            if (Fingerprint(ireg) != Fingerprint(twin.GetRegistry())) diverged = true;
            for (Core::PlayerId p = 1; p <= 2; ++p) {
                auto docks = ireg.view<const ECS::Components::Identity, const ECS::Components::Owner,
                                       const ECS::Components::Building>();
                for (auto d : docks) {
                    if (docks.get<const ECS::Components::Identity>(d).unit_id == dock_id &&
                        docks.get<const ECS::Components::Owner>(d).player_id == p &&
                        docks.get<const ECS::Components::Building>(d).state ==
                            ECS::Components::BuildingState::Complete) {
                        docked[p] = true;
                    }
                }
                if (trace) {
                    int ships = 0;
                    for (auto e : UnitsOf(ireg, ship_id)) {
                        if (ireg.get<ECS::Components::Owner>(e).player_id == p) ++ships;
                    }
                    const AI::Census c = AI::Take(ireg, p);
                    std::printf("AI water min %2d p%d dock %d ships %d fished %d landed %d "
                                "vil %zu (f%d w%d g%d s%d b%d) stock f%d w%d bld %zu\n",
                                minute, p, docked[p] ? 1 : 0, ships, fished[p] ? 1 : 0, landed[p],
                                c.villagers.size(), c.gatherers[0], c.gatherers[1], c.gatherers[2],
                                c.gatherers[3], c.builders, c.stock[0], c.stock[1],
                                c.buildings.size());
                }
            }
        }
        // Both build their Dock, and both fleets fish and land a catch.
        for (Core::PlayerId p = 1; p <= 2 && ok(); ++p) {
            if (!docked[p]) verdict = "the computer built no dock on Islands";
            else if (!fished[p]) verdict = "the computer's fishing ships never fished";
            else if (landed[p] == 0) verdict = "the computer's fishing ships landed no catch";
        }
        if (ok() && diverged) verdict = "two identical games on Islands came out differently";
    }

    std::printf("fishing=%s\n", verdict);
    return ok();
}

} // namespace Verify
