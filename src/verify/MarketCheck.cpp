#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Market.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"

namespace Verify {

namespace {

namespace R = Core::Rules;

entt::entity TownCenterOf(const entt::registry& reg, Core::PlayerId player) {
    const int tc = Core::ConfigManager::Get().GetUnitIdByName("RTWC");
    auto view = reg.view<const ECS::Components::Identity, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Identity>(e).unit_id == tc &&
            view.get<const ECS::Components::Owner>(e).player_id == player) {
            return e;
        }
    }
    return entt::null;
}

// A building of `unit_id` for `player` on open ground near its Town Center,
// finished or a bare foundation.
entt::entity Raise(ECS::Simulation& sim, Core::PlayerId player, int unit_id, bool complete) {
    auto& reg = sim.GetRegistry();
    const entt::entity tc = TownCenterOf(reg, player);
    if (tc == entt::null) return entt::null;
    const auto& at = reg.get<ECS::Components::Transform>(tc).position;
    for (int r = 5; r < 24; ++r) {
        for (int d = -r; d <= r; ++d) {
            const int x = Physics::TileOf(at.x) + d;
            const int y = Physics::TileOf(at.y) - r;
            if (ECS::Buildings::CanPlace(sim.GetGrid(), unit_id, x, y)) {
                return ECS::Buildings::Place(reg, sim.GetGrid(), unit_id, player, x, y, complete);
            }
        }
    }
    return entt::null;
}

int Whole(ECS::Simulation& sim, Core::PlayerId player, int dat_resource) {
    const ECS::Players::Player* seat = ECS::Players::Find(sim.GetRegistry(), player);
    return seat != nullptr ? ECS::Players::Resource(*seat, dat_resource).ToInt() : -1;
}

void Set(ECS::Simulation& sim, Core::PlayerId player, int dat_resource, int amount) {
    if (ECS::Players::Player* seat = ECS::Players::Find(sim.GetRegistry(), player)) {
        ECS::Players::SetResource(*seat, dat_resource, Core::Fixed32(amount));
    }
}

// An order through the queue and a tick, the way the interface gives one.
void Order(ECS::Simulation& sim, const Core::Command& command) {
    Core::CommandQueue::Get().PushCommand(command);
    sim.Update(16);
}

// What a lot costs or fetches at a base price and a commission in
// thousandths: the base less or plus the commission, rounded up. Worked out
// here independently of the module it checks.
int Expected(int base, int per_mille) {
    const int scaled = base * (1000 + per_mille);
    return (scaled + 999) / 1000;
}

} // namespace

// The Market: the starting prices, a lot sold and bought at the price on the
// button and the prices moving for every player, refusals that change
// nothing, five lots in one order at five prices, the floor, and Guilds
// cutting one player's commission.
bool CheckMarket() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int market_id = cfg.GetUnitIdByName("MRKT");
    const int food = R::Res::Food, wood = R::Res::Wood, stone = R::Res::Stone,
              gold = R::Res::Gold;

    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    const entt::entity market = Raise(sim, 1, market_id, true);
    const entt::entity site = Raise(sim, 2, market_id, false);
    if (market == entt::null || site == entt::null) verdict = "no ground to raise a Market on";

    // The opening prices: 70 and 130 for food and wood, 91 and 169 for stone.
    if (ok() && (ECS::Market::SellPrice(reg, 1, wood) != 70 ||
                 ECS::Market::BuyPrice(reg, 1, wood) != 130 ||
                 ECS::Market::SellPrice(reg, 1, food) != 70 ||
                 ECS::Market::BuyPrice(reg, 1, food) != 130 ||
                 ECS::Market::SellPrice(reg, 1, stone) != 91 ||
                 ECS::Market::BuyPrice(reg, 1, stone) != 169)) {
        verdict = "the opening prices are not 70/130 and 91/169";
    }
    if (ok() && (ECS::Market::BuyPrice(reg, 1, gold) != 0 || ECS::Market::Tradable(gold))) {
        verdict = "gold is offered for sale";
    }

    // A lot of wood sold: 100 wood off, the 70 on the button on, and the price
    // down for everyone -- the base 97, selling for 68 and buying for 127.
    if (ok()) {
        Set(sim, 1, wood, 300);
        Set(sim, 1, gold, 0);
        Set(sim, 1, food, 0);
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(wood), 1, false, 1});
        if (Whole(sim, 1, wood) != 200 || Whole(sim, 1, gold) != 70) {
            verdict = "selling 100 wood did not pay the 70 on the button";
        } else if (ECS::Market::BasePrice(reg, wood) != 97 ||
                   ECS::Market::SellPrice(reg, 1, wood) != 68 ||
                   ECS::Market::BuyPrice(reg, 1, wood) != 127) {
            verdict = "a sale did not move the price to 68/127";
        } else if (ECS::Market::SellPrice(reg, 2, wood) != 68) {
            verdict = "the price moved for the seller only";
        } else if (ECS::Market::BasePrice(reg, food) != 100) {
            verdict = "selling wood moved the price of food";
        }
    }

    // Refusals change nothing: food it does not have, food it cannot pay
    // for, a Market that is not its own, one still a foundation, gold, and
    // an order of no lots.
    if (ok()) {
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(food), 1, false, 1});
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(food), 1, true, 1});
        Set(sim, 2, wood, 500);
        Set(sim, 2, gold, 500);
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(wood), 1, false, 2});
        Order(sim, Core::MarketCommand{site, static_cast<uint8_t>(wood), 1, false, 2});
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(gold), 1, false, 1});
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(wood), 0, false, 1});
        if (Whole(sim, 1, food) != 0 || Whole(sim, 1, gold) != 70 || Whole(sim, 1, wood) != 200) {
            verdict = "a refused trade changed the seller's stockpile";
        } else if (Whole(sim, 2, wood) != 500 || Whole(sim, 2, gold) != 500) {
            verdict = "a player traded at another's Market, or at a foundation";
        } else if (ECS::Market::BasePrice(reg, food) != 100 ||
                   ECS::Market::BasePrice(reg, wood) != 97) {
            verdict = "a refused trade moved a price";
        }
    }

    // Five lots of food bought in one order, each at the price of its moment.
    if (ok()) {
        Set(sim, 1, gold, 1000);
        int paid = 0;
        for (int lot = 0; lot < 5; ++lot) paid += Expected(100 + 3 * lot, 300);
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(food), 5, true, 1});
        if (Whole(sim, 1, food) != 500 || Whole(sim, 1, gold) != 1000 - paid) {
            verdict = "five lots were not bought at five rising prices";
        } else if (ECS::Market::BasePrice(reg, food) != 115) {
            verdict = "five lots bought did not raise the price by 15";
        }
    }

    // More lots than the gold covers: it stops at the last one it can pay.
    if (ok()) {
        const int price = ECS::Market::BuyPrice(reg, 1, food);
        Set(sim, 1, gold, price + 10);
        Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(food), 5, true, 1});
        if (Whole(sim, 1, food) != 600 || Whole(sim, 1, gold) != 10) {
            verdict = "buying past the gold in hand bought more, or less, than it could pay for";
        }
    }

    // The floor: stone sold until it can fall no further.
    if (ok()) {
        Set(sim, 1, stone, 100000);
        for (int order = 0; order < 10; ++order) {
            Order(sim, Core::MarketCommand{market, static_cast<uint8_t>(stone), 5, false, 1});
        }
        if (ECS::Market::BasePrice(reg, stone) != 20 ||
            ECS::Market::SellPrice(reg, 1, stone) != 14 ||
            ECS::Market::BuyPrice(reg, 1, stone) != 26) {
            verdict = "stone sold off did not stop at a base of 20 (14/26)";
        }
    }

    // Guilds: the researcher's commission falls to the dat's 0.15, and only
    // its own.
    if (ok()) {
        const int guilds = 15;
        ECS::Techs::Complete(reg, 1, guilds);
        const int base = ECS::Market::BasePrice(reg, food);
        if (ECS::Market::SellPrice(reg, 1, food) != Expected(base, -150) ||
            ECS::Market::BuyPrice(reg, 1, food) != Expected(base, 150)) {
            verdict = "Guilds did not bring the commission down to 15%";
        } else if (ECS::Market::SellPrice(reg, 2, food) != Expected(base, -300)) {
            verdict = "one player's Guilds changed another's prices";
        }
    }

    std::printf("market=%s\n", verdict);
    return ok();
}

} // namespace Verify
