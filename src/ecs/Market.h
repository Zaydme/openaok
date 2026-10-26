#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Types.h"

// Buying and selling at the Market: food, wood and stone, in lots of 100, for
// gold.
//
// The prices are the whole game's, not a player's: they move with what every
// player buys and sells, after each lot. The dat carries only each player's
// commission -- resource 78, the "trade vig rate", 0.3, which Guilds sets to
// 0.15 and the Saracens' bonus to 0.05.
//
// - every commodity has one base price, 100 gold a lot for food and wood and
//   130 for stone at the start;
// - a lot sells for the base price less the seller's commission and buys for
//   the base price plus the buyer's -- 70 and 130 at the start, rounded up
//   (a base of 97 sells for 68 and buys for 127);
// - each lot sold takes 3 off its base price and each bought puts 3 on, the
//   base never below 20 nor above 9999.
//
// A trade moves whole gold: the price shown on the button.
//
// Simulation state, in the registry's context, like the players' table.
namespace ECS::Market {

// What the base prices start at, are moved by, and are kept between.
constexpr int kLot = 100;
constexpr int kStartingFoodPrice = 100;
constexpr int kStartingWoodPrice = 100;
constexpr int kStartingStonePrice = 130;
constexpr int kPriceStep = 3;
constexpr int kLowestPrice = 20;
constexpr int kHighestPrice = 9999;
// The most lots one order may carry: an order is for 1 or 5.
constexpr int kMostLots = 5;

// The base price of each commodity, by the dat's resource id: 0 food, 1 wood,
// 2 stone.
struct Prices {
    int base[3] = {kStartingFoodPrice, kStartingWoodPrice, kStartingStonePrice};
};

void Setup(entt::registry& registry);

// Whether the Market deals in this dat resource: food, wood and stone. Gold
// is what they are bought and sold for.
bool Tradable(int dat_resource);

// The commodity's base price now; 0 for one the Market does not deal in.
int BasePrice(const entt::registry& registry, int dat_resource);

// The player's commission: the dat's resource 78.
Core::Fixed32 Commission(const entt::registry& registry, Core::PlayerId player);

// What the player is paid for a lot of it, and what one costs it, in gold --
// the figures on its Market's buttons. 0 for what the Market does not deal in.
int SellPrice(const entt::registry& registry, Core::PlayerId player, int dat_resource);
int BuyPrice(const entt::registry& registry, Core::PlayerId player, int dat_resource);

// Whether `market` is a finished Market of the player's: the one place it may
// trade at.
bool IsMarket(const entt::registry& registry, entt::entity market, Core::PlayerId player);

enum class Refusal : uint8_t {
    None,          // done
    NoMarket,      // not a finished Market of the player's
    NotTradable,   // gold, or something that is not a stockpile
    CannotPay,     // fewer than 100 of it to sell, or not the gold to buy one
    BadLots,       // no lots, or more than one order may carry
};

// Sells, or buys, `lots` lots of the resource at `market`, one after another,
// each at the price of the moment -- the price moves with every lot. Refused
// outright, changing nothing, unless the first lot can be paid for; stops at
// the first one that cannot.
Refusal Sell(entt::registry& registry, entt::entity market, Core::PlayerId player,
             int dat_resource, int lots);
Refusal Buy(entt::registry& registry, entt::entity market, Core::PlayerId player,
            int dat_resource, int lots);

} // namespace ECS::Market
