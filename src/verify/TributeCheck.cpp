#include "verify/Harness.h"

#include <cstdio>
#include <cstring>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Rules.h"
#include "ecs/Buildings.h"
#include "ecs/Events.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Techs.h"
#include "ecs/Tribute.h"
#include "verify/Shore.h"

namespace Verify {

namespace {

using Core::Fixed32;
namespace R = Core::Rules;

void Order(ECS::Simulation& sim, const Core::Command& command) {
    Core::CommandQueue::Get().PushCommand(command);
    sim.Update(16);
}

Fixed32 Held(ECS::Simulation& sim, Core::PlayerId player, int dat_resource) {
    const auto* seat = ECS::Players::Find(sim.GetRegistry(), player);
    return seat != nullptr ? ECS::Players::Resource(*seat, dat_resource) : Fixed32(-1);
}

void Set(ECS::Simulation& sim, Core::PlayerId player, int dat_resource, int amount) {
    if (auto* seat = ECS::Players::Find(sim.GetRegistry(), player)) {
        ECS::Players::SetResource(*seat, dat_resource, Fixed32(amount));
    }
}

// Whether the event log holds a tribute from `from` to `to` of `amount` of
// `resource` on the last tick.
bool Announced(ECS::Simulation& sim, Core::PlayerId from, Core::PlayerId to, int resource,
               int amount) {
    const auto* log = ECS::Events::Read(sim.GetRegistry());
    if (log == nullptr) return false;
    for (const auto& e : log->events) {
        if (e.tick == log->tick && e.kind == ECS::Events::Kind::Tribute && e.from == from &&
            e.player == to && e.id == resource && e.amount == amount) {
            return true;
        }
    }
    return false;
}

Core::GameSetup ThreePlayers(bool locked) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[1].team = 1;
    setup.players[2].team = 1;
    setup.players[3] = {true, 7, 0};   // Byzantines, on no team
    setup.lock_teams = locked;
    return setup;
}

} // namespace

// Tribute: a Market needed, the 30% fee on top, Coinage bringing it to 20%
// and Banking to nothing, the recipient paid the amount, news of it; refusals
// that change nothing -- no Market, more than the giver holds, nothing, to
// itself, to Gaia, not a stockpile, and with teams locked, to anyone not an
// ally -- and, unlocked, a tribute to an enemy.
bool CheckTribute() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };
    auto& cfg = Core::ConfigManager::Get();
    const int market_id = cfg.GetUnitIdByName("MRKT");
    const int food = R::Res::Food, wood = R::Res::Wood, gold = R::Res::Gold;

    {
        ECS::Simulation sim;
        sim.Init(ThreePlayers(true));
        auto& reg = sim.GetRegistry();
        Set(sim, 1, gold, 1000);
        Set(sim, 2, gold, 0);
        Set(sim, 3, gold, 0);

        // No Market: nothing goes.
        Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), 100, 1});
        if (Held(sim, 1, gold) != Fixed32(1000) || Held(sim, 2, gold) != Fixed32(0)) {
            verdict = "a tribute went without a Market";
        }
        const auto [sx, sy] = sim.player_starts[0];
        if (ok() && Shore::RaiseNear(sim, 1, market_id, sx + 8, sy + 2, 14) == entt::null) {
            verdict = "no ground for a Market";
        }

        // 100 gold to an ally: 130 off, 100 on, and the news.
        if (ok()) {
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), 100, 1});
            if (Held(sim, 1, gold) != Fixed32(870)) {
                verdict = "a tribute of 100 did not cost the giver 130";
            } else if (Held(sim, 2, gold) != Fixed32(100)) {
                verdict = "the recipient was not given the 100";
            } else if (!Announced(sim, 1, 2, gold, 100)) {
                verdict = "a tribute went unannounced";
            } else if (Held(sim, 1, 53) != Fixed32(100) || Held(sim, 1, 103) != Fixed32(100)) {
                verdict = "the tribute given was not kept";
            }
        }
        // Wood goes the same way, by its own stockpile.
        if (ok()) {
            Set(sim, 1, wood, 200);
            const Fixed32 wood2 = Held(sim, 2, wood);
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(wood), 50, 1});
            if (Held(sim, 1, wood) != Fixed32(135) || Held(sim, 2, wood) - wood2 != Fixed32(50)) {
                verdict = "a tribute of 50 wood did not cost 65";
            }
        }

        // Refusals: to the enemy while teams are locked, more than it holds,
        // nothing, to itself, to Gaia, gold-and-then-some that is not a
        // stockpile.
        if (ok()) {
            Order(sim, Core::TributeCommand{3, static_cast<uint8_t>(gold), 100, 1});
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), 700, 1});
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), 0, 1});
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), -50, 1});
            Order(sim, Core::TributeCommand{1, static_cast<uint8_t>(gold), 100, 1});
            Order(sim, Core::TributeCommand{0, static_cast<uint8_t>(gold), 100, 1});
            Order(sim, Core::TributeCommand{2, 4, 100, 1});
            if (Held(sim, 1, gold) != Fixed32(870) || Held(sim, 2, gold) != Fixed32(100) ||
                Held(sim, 3, gold) != Fixed32(0)) {
                verdict = "a refused tribute moved gold";
            } else if (ECS::Tribute::Check(reg, 1, 3, gold, 100) !=
                       ECS::Tribute::Refusal::NotAllowed) {
                verdict = "a tribute to an enemy was not refused with teams locked";
            } else if (ECS::Tribute::Check(reg, 1, 2, gold, 700) !=
                       ECS::Tribute::Refusal::CannotPay) {
                verdict = "a tribute past what the giver holds was not refused for it";
            }
        }

        // Coinage: the fee down to 20%. Banking: none.
        if (ok()) {
            ECS::Techs::Complete(reg, 1, 23);
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), 100, 1});
            if (Held(sim, 1, gold) != Fixed32(750)) verdict = "Coinage did not bring the fee to 20%";
        }
        if (ok()) {
            ECS::Techs::Complete(reg, 1, 17);
            Order(sim, Core::TributeCommand{2, static_cast<uint8_t>(gold), 100, 1});
            if (Held(sim, 1, gold) != Fixed32(650)) verdict = "Banking did not take the fee away";
            else if (Held(sim, 2, gold) != Fixed32(300)) verdict = "the ally was not paid each tribute";
        }
        // Another player's fee is its own.
        if (ok() && ECS::Tribute::Cost(reg, 2, 100) != Fixed32(130)) {
            verdict = "one player's Banking changed another's fee";
        }
    }

    // Teams unlocked: a tribute may go to anyone, enemies too.
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(ThreePlayers(false));
        const auto [sx, sy] = sim.player_starts[0];
        Shore::RaiseNear(sim, 1, market_id, sx + 8, sy + 2, 14);
        Set(sim, 1, food, 400);
        const Fixed32 before = Held(sim, 3, food);
        Order(sim, Core::TributeCommand{3, static_cast<uint8_t>(food), 300, 1});
        if (Held(sim, 1, food) != Fixed32(10) || Held(sim, 3, food) - before != Fixed32(300)) {
            verdict = "with teams unlocked a tribute to an enemy did not go";
        }
    }

    std::printf("tribute=%s\n", verdict);
    return ok();
}

} // namespace Verify
