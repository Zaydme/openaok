#include "ecs/Market.h"

#include <algorithm>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"

namespace ECS::Market {

namespace {

namespace R = Core::Rules;

// The dat's "trade vig rate": the commission on a lot bought or sold.
constexpr int kCommission = 78;

// A commission in thousandths, as the dat states it (0.3, 0.15, 0.05). The
// table holds it in fixed point converted from thousandths, so it is rounded
// back rather than truncated: 0.3 is a hair under 0.3 in binary.
int CommissionThousandths(const entt::registry& registry, Core::PlayerId player) {
    const Core::Fixed32 rate = Commission(registry, player);
    return (rate * Core::Fixed32(1000) + Core::Fixed32(1) / Core::Fixed32(2)).ToInt();
}

// `base` times (1000 + `per_mille`) / 1000, rounded up, in whole numbers: the
// price on the button, with no fixed-point dust to tip a 70 into a 71.
int RoundedUp(int base, int per_mille) {
    const int64_t scaled = static_cast<int64_t>(base) * (1000 + per_mille);
    return static_cast<int>((scaled + 999) / 1000);
}

Prices* Table(entt::registry& registry) {
    return registry.ctx().find<Prices>();
}

void Move(entt::registry& registry, int dat_resource, int step) {
    if (Prices* prices = Table(registry)) {
        int& base = prices->base[dat_resource];
        base = std::clamp(base + step, kLowestPrice, kHighestPrice);
    }
}

// Lots, one after another, each at the price of the moment. `buy` pays gold
// for the commodity; otherwise the commodity is paid for gold.
Refusal Deal(entt::registry& registry, entt::entity market, Core::PlayerId player,
             int dat_resource, int lots, bool buy) {
    if (!IsMarket(registry, market, player)) return Refusal::NoMarket;
    if (!Tradable(dat_resource)) return Refusal::NotTradable;
    if (lots <= 0 || lots > kMostLots) return Refusal::BadLots;
    Players::Player* seat = Players::Find(registry, player);
    if (seat == nullptr) return Refusal::NoMarket;

    int done = 0;
    for (; done < lots; ++done) {
        if (buy) {
            const int price = BuyPrice(registry, player, dat_resource);
            if (Players::Resource(*seat, R::Res::Gold) < Core::Fixed32(price)) break;
            Players::AddResource(*seat, R::Res::Gold, Core::Fixed32(-price));
            Players::AddResource(*seat, dat_resource, Core::Fixed32(kLot));
            Move(registry, dat_resource, kPriceStep);
        } else {
            if (Players::Resource(*seat, dat_resource) < Core::Fixed32(kLot)) break;
            const int price = SellPrice(registry, player, dat_resource);
            Players::AddResource(*seat, dat_resource, Core::Fixed32(-kLot));
            Players::AddResource(*seat, R::Res::Gold, Core::Fixed32(price));
            Move(registry, dat_resource, -kPriceStep);
        }
    }
    if (done == 0) return Refusal::CannotPay;
    Events::Emit(registry, {.kind = Events::Kind::Traded,
                            .subject = market,
                            .player = player,
                            .id = dat_resource,
                            .amount = buy ? done : -done});
    return Refusal::None;
}

} // namespace

void Setup(entt::registry& registry) {
    registry.ctx().insert_or_assign(Prices{});
}

bool Tradable(int dat_resource) {
    return dat_resource == R::Res::Food || dat_resource == R::Res::Wood ||
           dat_resource == R::Res::Stone;
}

int BasePrice(const entt::registry& registry, int dat_resource) {
    if (!Tradable(dat_resource)) return 0;
    const Prices* prices = registry.ctx().find<Prices>();
    return prices != nullptr ? prices->base[dat_resource] : Prices{}.base[dat_resource];
}

Core::Fixed32 Commission(const entt::registry& registry, Core::PlayerId player) {
    const Players::Player* seat = Players::Find(registry, player);
    return seat != nullptr ? Players::Resource(*seat, kCommission) : Core::Fixed32(0);
}

int SellPrice(const entt::registry& registry, Core::PlayerId player, int dat_resource) {
    if (!Tradable(dat_resource)) return 0;
    return RoundedUp(BasePrice(registry, dat_resource),
                     -CommissionThousandths(registry, player));
}

int BuyPrice(const entt::registry& registry, Core::PlayerId player, int dat_resource) {
    if (!Tradable(dat_resource)) return 0;
    return RoundedUp(BasePrice(registry, dat_resource),
                     CommissionThousandths(registry, player));
}

bool IsMarket(const entt::registry& registry, entt::entity market, Core::PlayerId player) {
    if (!registry.valid(market)) return false;
    const auto* owner = registry.try_get<Components::Owner>(market);
    const auto* identity = registry.try_get<Components::Identity>(market);
    const auto* building = registry.try_get<Components::Building>(market);
    if (owner == nullptr || identity == nullptr || building == nullptr) return false;
    if (owner->player_id != player) return false;
    if (building->state != Components::BuildingState::Complete) return false;
    // The Market keeps its base id through the ages' art, as every building
    // does.
    return identity->unit_id == Core::ConfigManager::Get().GetUnitIdByName("MRKT");
}

Refusal Sell(entt::registry& registry, entt::entity market, Core::PlayerId player,
             int dat_resource, int lots) {
    return Deal(registry, market, player, dat_resource, lots, false);
}

Refusal Buy(entt::registry& registry, entt::entity market, Core::PlayerId player,
            int dat_resource, int lots) {
    return Deal(registry, market, player, dat_resource, lots, true);
}

} // namespace ECS::Market
