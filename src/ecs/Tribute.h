#pragma once
#include <cstdint>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Types.h"

// Tribute: resources given outright to another player.
//
// A player with a Market can send wood, food, gold or stone from its
// stockpile to another player, paying a fee on top: a tribute of 100 gold
// takes 130 from the giver. With teams locked, tribute goes only to allies.
//
// The fee is the dat's: the giver's resource 46, "tribute inefficiency", 0.3,
// which Coinage sets to 0.2 and Banking to 0. What a player has given is kept
// in resource 53 and, per recipient, 102 onwards.
namespace ECS::Tribute {

// The giver's fee: the share of a tribute lost on top of it.
Core::Fixed32 Fee(const entt::registry& registry, Core::PlayerId player);

// What a tribute of `amount` takes from the giver's stockpile: the amount and
// the fee on it.
Core::Fixed32 Cost(const entt::registry& registry, Core::PlayerId player, int amount);

enum class Refusal : uint8_t {
    None,
    NoMarket,       // the giver has no finished Market
    NotAllowed,     // to itself, to Gaia, to no one, to one defeated -- or, with
                    // teams locked, to anyone not its ally
    NotTribute,     // not one of the four stockpiles
    BadAmount,      // nothing, or less
    CannotPay,      // the amount and its fee are more than the giver holds
};

// Whether `from` may give `to` a tribute of `amount` of the dat resource.
Refusal Check(const entt::registry& registry, Core::PlayerId from, Core::PlayerId to,
              int dat_resource, int amount);

// Gives it, if it may: the cost from the giver, the amount to the recipient,
// and news of it. Refused outright otherwise, changing nothing.
Refusal Send(entt::registry& registry, Core::PlayerId from, Core::PlayerId to,
             int dat_resource, int amount);

} // namespace ECS::Tribute
