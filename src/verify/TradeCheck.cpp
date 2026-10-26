#include "verify/Harness.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Buildings.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Production.h"
#include "ecs/Simulation.h"
#include "ecs/Trade.h"
#include "ecs/Units.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Trader.h"
#include "ecs/components/Transform.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Components::Trader;
using ECS::Components::Vector2D;
namespace R = Core::Rules;

constexpr int kTicksPerSecond = 1000 / 16;

void Order(ECS::Simulation& sim, const Core::Command& command) {
    Core::CommandQueue::Get().PushCommand(command);
    sim.Update(16);
}

Fixed32 Gold(ECS::Simulation& sim, Core::PlayerId player) {
    const auto* seat = ECS::Players::Find(sim.GetRegistry(), player);
    return seat != nullptr ? ECS::Players::Resource(*seat, R::Res::Gold) : Fixed32(-1);
}

Fixed32 Profit(ECS::Simulation& sim, Core::PlayerId player) {
    const auto* seat = ECS::Players::Find(sim.GetRegistry(), player);
    return seat != nullptr ? ECS::Players::Resource(*seat, 101) : Fixed32(-1);
}

// The middle of a building's ground.
void MiddleOf(const entt::registry& reg, entt::entity b, double& x, double& y) {
    const auto ground = ECS::Buildings::FootprintBounds(
        reg.get<ECS::Components::Transform>(b).position,
        reg.get<ECS::Components::Identity>(b).unit_id);
    x = ((ground.min_x + ground.max_x) / Fixed32(2)).ToFloat();
    y = ((ground.min_y + ground.max_y) / Fixed32(2)).ToFloat();
}

// The expected pay, worked in plain doubles independently of the module it
// checks.
double Formula(const entt::registry& reg, entt::entity a, entt::entity b, int map_tiles) {
    double ax = 0, ay = 0, bx = 0, by = 0;
    MiddleOf(reg, a, ax, ay);
    MiddleOf(reg, b, bx, by);
    const double dx = std::max(0.0, std::fabs(ax - bx) - 4.0);
    const double dy = std::max(0.0, std::fabs(ay - by) - 4.0);
    const double d = std::sqrt(dx * dx + dy * dy);
    return (d / map_tiles + 0.3) * d * 0.46 + 0.5;
}

bool Near(Fixed32 got, double want) {
    return std::fabs(got.ToFloat() - want) < 0.01;
}

const Trader* TraderOf(const entt::registry& reg, entt::entity unit) {
    return reg.valid(unit) ? reg.try_get<Trader>(unit) : nullptr;
}

int UnitIdOf(const entt::registry& reg, entt::entity e) {
    return reg.valid(e) ? reg.get<ECS::Components::Identity>(e).unit_id : -1;
}

// A unit of `unit_id` for `player` on the doorstep of `building`, on the
// unit's own ground.
entt::entity SpawnAt(ECS::Simulation& sim, entt::entity building, int unit_id,
                     Core::PlayerId player) {
    auto& reg = sim.GetRegistry();
    Vector2D at{};
    if (!ECS::Buildings::Doorstep(reg, sim.GetGrid(), building, at,
                                  ECS::Navigation::LayerOfUnit(reg, sim.GetGrid(), player,
                                                               unit_id))) {
        return entt::null;
    }
    return ECS::Units::Spawn(reg, unit_id, player, at);
}

// Runs until the trader's load changes to `loaded`, or `limit` seconds pass.
// False if it never did.
bool RunUntilLoaded(ECS::Simulation& sim, entt::entity unit, bool loaded, int limit) {
    for (int t = 0; t < limit * kTicksPerSecond; ++t) {
        sim.Update(16);
        const Trader* trader = TraderOf(sim.GetRegistry(), unit);
        if (trader == nullptr) return false;
        if (trader->loaded == loaded) return true;
    }
    return false;
}

// One trade unit's whole round trip, twice: out to the foreign building,
// back with the load the formula says, turning into its full self and back,
// and the gold paid in at home -- then the same again. Returns a verdict.
const char* RoundTrip(ECS::Simulation& sim, entt::entity unit, entt::entity home,
                      entt::entity partner, int empty_id, int full_id, int limit) {
    auto& reg = sim.GetRegistry();
    const double expected = Formula(reg, home, partner, sim.GetGrid().GetWidth());
    Order(sim, Core::TradeCommand{unit, partner, 1});
    const Trader* trader = TraderOf(reg, unit);
    if (trader == nullptr || !trader->active || trader->partner != partner || trader->home != home) {
        return "a trade order did not put the unit on the route";
    }
    for (int trip = 0; trip < 2; ++trip) {
        const Fixed32 before = Gold(sim, 1);
        const Fixed32 profit = Profit(sim, 1);
        if (!RunUntilLoaded(sim, unit, true, limit)) return "a trader never took on its gold";
        trader = TraderOf(reg, unit);
        const Fixed32 load = trader->gold;
        if (!Near(load, expected)) return "a load did not pay what the formula says";
        if (UnitIdOf(reg, unit) != full_id) return "a loaded trader did not change into its full self";
        if (Gold(sim, 1) != before) return "gold was paid in before the trader came home";
        if (!RunUntilLoaded(sim, unit, false, limit)) return "a loaded trader never came home";
        trader = TraderOf(reg, unit);
        if (Gold(sim, 1) - before != load) return "the gold handed in at home was not the load";
        if (Profit(sim, 1) - profit != load) return "trade profit was not kept";
        if (UnitIdOf(reg, unit) != empty_id) return "an unloaded trader did not change back";
        if (!trader->active) return "a trader stopped after one trip";
    }
    return "ok";
}

} // namespace

// Trade, by the dat's tasks and the pay formula: who may trade where and
// with whom, a trade cart's two round trips between Markets paid what the
// formula says each time, a full cart that is the dat's own unit, the
// payment growing with the distance, a partner lost and a home lost, a cart
// rallied onto a foreign Market trading as it comes out, and a trade cog's
// round trip between Docks across the sea.
bool CheckTrade() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int market_id = cfg.GetUnitIdByName("MRKT");
    const int cart_id = cfg.GetUnitIdByName("TCART");
    const int full_id = cfg.GetUnitIdByName("TCARTF");
    const int dock_id = cfg.GetUnitIdByName("DOCK");
    const int cog_id = cfg.GetUnitIdByName("COGXX");
    const int villager_id = cfg.GetUnitIdByName("VMBAS");

    {
        ECS::Simulation sim;
        sim.Init();
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        const auto [sx, sy] = sim.player_starts[0];
        // Player 1's Market near its town, and player 2's a fair way off --
        // twenty-odd tiles, so a trip is short enough to watch twice.
        const entt::entity home = Shore::RaiseNear(sim, 1, market_id, sx + 8, sy + 2, 12);
        const entt::entity partner = Shore::RaiseNear(sim, 2, market_id, sx + 8, sy + 26, 14);
        const entt::entity far = Shore::RaiseNear(sim, 2, market_id, sx + 8, sy + 44, 14);
        // Next door: touching player 1's, on whichever side has room.
        entt::entity next_door = entt::null;
        if (home != entt::null) {
            const auto& at = reg.get<ECS::Components::Transform>(home).position;
            const int hx = at.x.ToInt(), hy = at.y.ToInt();
            const int sides[4][2] = {{4, 0}, {0, 4}, {-4, 0}, {0, -4}};
            for (const auto& side : sides) {
                if (next_door != entt::null) break;
                next_door = Shore::RaiseNear(sim, 2, market_id, hx + side[0], hy + side[1], 0);
            }
        }
        int px = 0, py = 0;
        Shore::NearestSpot(grid, market_id, sx - 8, sy + 8, 12, px, py);
        const entt::entity site = ECS::Buildings::Place(reg, grid, market_id, 2, px, py, false);
        const entt::entity cart = home != entt::null ? SpawnAt(sim, home, cart_id, 1) : entt::null;
        const entt::entity villager = home != entt::null ? SpawnAt(sim, home, villager_id, 1)
                                                         : entt::null;
        if (home == entt::null || partner == entt::null || far == entt::null ||
            next_door == entt::null || site == entt::null || cart == entt::null ||
            villager == entt::null) {
            verdict = "no ground for the Markets, or no cart";
        }

        // Who may trade where: never at one's own, nor at a foundation, and
        // only what has a trade task.
        if (ok()) {
            if (ECS::Trade::CanTrade(reg, cart, home)) verdict = "a cart may trade with its own Market";
            else if (ECS::Trade::CanTrade(reg, cart, site)) verdict = "a cart may trade with a foundation";
            else if (ECS::Trade::CanTrade(reg, villager, partner)) verdict = "a villager may trade";
            else if (!ECS::Trade::CanTrade(reg, cart, partner)) verdict = "a cart may not trade with a foreign Market";
            Order(sim, Core::TradeCommand{cart, home, 1});
            Order(sim, Core::TradeCommand{cart, partner, 2});
            if (ok() && TraderOf(reg, cart) != nullptr) verdict = "a refused trade order started a route";
        }

        // The pay: half a gold next door, more the further apart, and the
        // formula's figure for each.
        if (ok()) {
            const Fixed32 short_pay = ECS::Trade::Pay(reg, grid, home, partner);
            const Fixed32 long_pay = ECS::Trade::Pay(reg, grid, home, far);
            const Fixed32 none = ECS::Trade::Pay(reg, grid, home, next_door);
            if (!Near(short_pay, Formula(reg, home, partner, grid.GetWidth())) ||
                !Near(long_pay, Formula(reg, home, far, grid.GetWidth()))) {
                verdict = "the pay does not follow the trade formula";
            } else if (long_pay <= short_pay) {
                verdict = "a longer route does not pay more";
            } else if (!Near(none, 0.5)) {
                verdict = "Markets side by side do not pay the half gold";
            }
        }

        if (ok()) verdict = RoundTrip(sim, cart, home, partner, cart_id, full_id, 120);

        // The partner destroyed while the cart is on its way out: it goes
        // home and waits.
        if (ok()) {
            if (!RunUntilLoaded(sim, cart, false, 1)) verdict = "the cart was not on its way out";
            for (int t = 0; t < 3 * kTicksPerSecond; ++t) sim.Update(16);
            ECS::Buildings::Destroy(reg, grid, partner);
            for (int t = 0; t < 60 * kTicksPerSecond; ++t) sim.Update(16);
            const Trader* trader = TraderOf(reg, cart);
            const Fixed32 gap = ECS::Buildings::FootprintBounds(
                reg.get<ECS::Components::Transform>(home).position, market_id)
                .DistanceFrom(reg.get<ECS::Components::Transform>(cart).position);
            if (ok() && (trader == nullptr || trader->active)) {
                verdict = "a cart kept trading with a destroyed Market";
            } else if (ok() && gap > Fixed32(2)) {
                verdict = "a cart whose partner was destroyed did not go home";
            }
        }

        // Its home destroyed while it carries gold, and no other: the gold is
        // lost and it stops.
        if (ok()) {
            Order(sim, Core::TradeCommand{cart, far, 1});
            if (!RunUntilLoaded(sim, cart, true, 120)) {
                verdict = "the cart never loaded at the far Market";
            } else {
                const Fixed32 before = Gold(sim, 1);
                ECS::Buildings::Destroy(reg, grid, home);
                for (int t = 0; t < 2 * kTicksPerSecond; ++t) sim.Update(16);
                const Trader* trader = TraderOf(reg, cart);
                if (trader == nullptr || trader->active || trader->loaded ||
                    trader->gold != Fixed32(0)) {
                    verdict = "a cart kept its gold with no Market of its own left";
                } else if (UnitIdOf(reg, cart) != cart_id) {
                    verdict = "a cart that lost its gold stayed full";
                } else if (Gold(sim, 1) != before) {
                    verdict = "a cart with no Market to go home to paid in its gold";
                }
            }
        }
    }

    // Rallied onto a foreign Market, a cart trades as it comes out.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.starting_age = 1;
        ECS::Simulation sim;
        sim.Init(setup);
        auto& reg = sim.GetRegistry();
        const auto [sx, sy] = sim.player_starts[0];
        const entt::entity home = Shore::RaiseNear(sim, 1, market_id, sx + 8, sy + 2, 12);
        const entt::entity partner = Shore::RaiseNear(sim, 2, market_id, sx + 8, sy + 26, 14);
        if (auto* seat = ECS::Players::Find(reg, 1)) {
            ECS::Players::SetResource(*seat, R::Res::Wood, Fixed32(500));
            ECS::Players::SetResource(*seat, R::Res::Gold, Fixed32(500));
        }
        const R::UnitRules* rules = ECS::Players::UnitOf(reg, 1, cart_id);
        if (home == entt::null || partner == entt::null) {
            verdict = "no ground for the rallied Markets";
        } else if (rules == nullptr || !rules->enabled) {
            verdict = "the Feudal Age did not make the trade cart available";
        } else {
            const auto& at = reg.get<ECS::Components::Transform>(partner).position;
            Order(sim, Core::RallyCommand{home, at.x, at.y, partner, 1});
            Order(sim, Core::TrainCommand{home, cart_id, 1});
            entt::entity born = entt::null;
            for (int t = 0; t < (rules->train_time + 5) * kTicksPerSecond && born == entt::null; ++t) {
                sim.Update(16);
                auto carts = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
                for (auto e : carts) {
                    if (carts.get<const ECS::Components::Identity>(e).unit_id == cart_id) born = e;
                }
            }
            const Trader* trader = TraderOf(reg, born);
            if (born == entt::null) verdict = "the Market trained no cart";
            else if (trader == nullptr || !trader->active || trader->partner != partner) {
                verdict = "a cart rallied onto a foreign Market did not trade";
            }
        }
    }

    // A trade cog between Docks on Islands, across the sea.
    if (ok()) {
        Core::GameSetup setup = Core::DefaultSetup();
        setup.map_script = "Islands";
        ECS::Simulation sim;
        sim.Init(setup);
        const auto [ax, ay] = sim.player_starts[0];
        const auto [bx, by] = sim.player_starts[1];
        const entt::entity home = Shore::RaiseNear(sim, 1, dock_id, ax, ay, 40);
        const entt::entity partner = Shore::RaiseNear(sim, 2, dock_id, bx, by, 40);
        const entt::entity cog = home != entt::null ? SpawnAt(sim, home, cog_id, 1) : entt::null;
        if (home == entt::null || partner == entt::null || cog == entt::null) {
            verdict = "no shore for the Docks, or no cog";
        } else {
            const char* trip = RoundTrip(sim, cog, home, partner, cog_id, cog_id, 240);
            if (std::strcmp(trip, "ok") != 0) verdict = trip;
        }
    }

    std::printf("trade=%s\n", verdict);
    return ok();
}

} // namespace Verify
