#include "ecs/Tribute.h"

#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Market.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Victory.h"
#include "ecs/components/Building.h"
#include "ecs/components/Owner.h"

namespace ECS::Tribute {

namespace {

namespace R = Core::Rules;

constexpr int kTributeFee = 46;     // the dat's "tribute inefficiency"
constexpr int kTributeSent = 53;
constexpr int kTributeToFirst = 102;  // given to player 1; 103 to player 2 ...
// The most one tribute may give: the engine's own bound, far past any
// stockpile, so the fee's arithmetic cannot overflow.
constexpr int kMostTribute = 1000000;

bool IsTribute(int dat_resource) {
    return dat_resource == R::Res::Food || dat_resource == R::Res::Wood ||
           dat_resource == R::Res::Stone || dat_resource == R::Res::Gold;
}

bool HasMarket(const entt::registry& registry, Core::PlayerId player) {
    auto buildings = registry.view<const Components::Building, const Components::Owner>();
    for (auto b : buildings) {
        if (buildings.get<const Components::Owner>(b).player_id != player) continue;
        if (Market::IsMarket(registry, b, player)) return true;
    }
    return false;
}

} // namespace

Core::Fixed32 Fee(const entt::registry& registry, Core::PlayerId player) {
    const Players::Player* seat = Players::Find(registry, player);
    return seat != nullptr ? Players::Resource(*seat, kTributeFee) : Core::Fixed32(0);
}

Core::Fixed32 Cost(const entt::registry& registry, Core::PlayerId player, int amount) {
    // The fee in thousandths, as the dat states it (0.3, 0.2, 0), so that
    // 100 costs 130 to the gold and not a hair under.
    const int per_mille =
        (Fee(registry, player) * Core::Fixed32(1000) + Core::Fixed32(1) / Core::Fixed32(2)).ToInt();
    return Core::Fixed32(amount) +
           Core::Fixed32(amount * per_mille) / Core::Fixed32(1000);
}

Refusal Check(const entt::registry& registry, Core::PlayerId from, Core::PlayerId to,
              int dat_resource, int amount) {
    if (from == to || to == Core::PLAYER_GAIA || from == Core::PLAYER_GAIA) {
        return Refusal::NotAllowed;
    }
    const Players::Player* giver = Players::Find(registry, from);
    if (giver == nullptr || Players::Find(registry, to) == nullptr) return Refusal::NotAllowed;
    if (const Victory::Outcome* outcome = Victory::Read(registry)) {
        if (outcome->Defeated(from) || outcome->Defeated(to)) return Refusal::NotAllowed;
    }
    if (Diplomacy::Locked(registry) &&
        Diplomacy::StanceOf(registry, from, to) != Diplomacy::Stance::Ally) {
        return Refusal::NotAllowed;
    }
    if (!IsTribute(dat_resource)) return Refusal::NotTribute;
    if (amount <= 0 || amount > kMostTribute) return Refusal::BadAmount;
    if (!HasMarket(registry, from)) return Refusal::NoMarket;
    if (Players::Resource(*giver, dat_resource) < Cost(registry, from, amount)) {
        return Refusal::CannotPay;
    }
    return Refusal::None;
}

Refusal Send(entt::registry& registry, Core::PlayerId from, Core::PlayerId to,
             int dat_resource, int amount) {
    const Refusal refusal = Check(registry, from, to, dat_resource, amount);
    if (refusal != Refusal::None) return refusal;
    Players::Player* giver = Players::Find(registry, from);
    Players::Player* recipient = Players::Find(registry, to);
    const Core::Fixed32 cost = Cost(registry, from, amount);
    Players::AddResource(*giver, dat_resource, Core::Fixed32(0) - cost);
    Players::AddResource(*recipient, dat_resource, Core::Fixed32(amount));
    Players::AddResource(*giver, kTributeSent, Core::Fixed32(amount));
    Players::AddResource(*giver, kTributeToFirst + to - 1, Core::Fixed32(amount));
    // Sent and received, as the economy score nets them (ecs/Score).
    Score::Tributed(registry, from, to, amount);
    Events::Emit(registry, {.kind = Events::Kind::Tribute,
                            .player = to,
                            .id = dat_resource,
                            .amount = amount,
                            .from = from});
    return Refusal::None;
}

} // namespace ECS::Tribute
