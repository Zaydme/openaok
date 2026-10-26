#include "verify/Harness.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "core/Command.h"
#include "core/CommandQueue.h"
#include "core/GameSetup.h"
#include "ecs/Chat.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "ecs/Simulation.h"

namespace Verify {

namespace {

using Core::Fixed32;
using ECS::Diplomacy::Stance;

void Order(ECS::Simulation& sim, const Core::Command& command) {
    Core::CommandQueue::Get().PushCommand(command);
    sim.Update(16);
}

Core::GameSetup ThreePlayers(bool locked) {
    Core::GameSetup setup = Core::DefaultSetup();
    setup.players[3] = {true, 7, 0};
    setup.lock_teams = locked;
    if (locked) {
        setup.players[1].team = 1;
        setup.players[2].team = 1;
    }
    return setup;
}

size_t Lines(ECS::Simulation& sim) {
    const auto* log = ECS::Chat::Read(sim.GetRegistry());
    return log != nullptr ? log->lines.size() : 0;
}

const ECS::Chat::Line* Last(ECS::Simulation& sim) {
    const auto* log = ECS::Chat::Read(sim.GetRegistry());
    return log != nullptr && !log->lines.empty() ? &log->lines.back() : nullptr;
}

constexpr uint16_t Players(std::initializer_list<int> seats) {
    uint16_t bits = 0;
    for (int s : seats) bits = static_cast<uint16_t>(bits | (1u << s));
    return bits;
}

} // namespace

// Chat and flares, carried as orders: a message to everyone, to the sender's
// allies (by its own stance) and to one player; with teams locked, no private
// word to a non-ally; nothing from the defeated, nothing empty, a long line cut
// to 65 characters; taunts -- a number 1 to 30 alone; and a flare seen by the
// sender and its allies for the dat's seven seconds.
bool CheckChat() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };

    {
        ECS::Simulation sim;
        sim.Init(ThreePlayers(false));
        Order(sim, Core::DiplomacyCommand{2, static_cast<uint8_t>(Stance::Ally), 1});

        Order(sim, Core::ChatCommand{0, 0, "Wood, please", 1});
        const auto* line = Last(sim);
        if (line == nullptr || line->to != Players({1, 2, 3}) || line->text != "Wood, please" ||
            line->from != 1 || line->taunt != 0) {
            verdict = "a message to everyone did not reach everyone";
        }
        if (ok()) {
            Order(sim, Core::ChatCommand{1, 0, "attack now", 1});
            line = Last(sim);
            if (line == nullptr || line->to != Players({1, 2})) {
                verdict = "a message to allies did not reach just the sender's allies";
            }
        }
        if (ok()) {
            // Player 2 does not regard player 1 as an ally: its word to its
            // allies is for itself alone.
            Order(sim, Core::ChatCommand{1, 0, "anyone?", 2});
            line = Last(sim);
            if (line == nullptr || line->to != Players({2})) {
                verdict = "allies were taken from the other player's stance";
            }
        }
        if (ok()) {
            Order(sim, Core::ChatCommand{2, 3, "surrender", 1});
            line = Last(sim);
            if (line == nullptr || line->to != Players({1, 3})) {
                verdict = "a message to one player did not reach just that player";
            }
        }
        // Taunts: a number from 1 to 30, alone.
        if (ok()) {
            const struct { const char* text; int taunt; } cases[] = {
                {"1", 1}, {"30", 30}, {" 14 ", 14}, {"31", 0}, {"0", 0}, {"11a", 0}, {"gg", 0},
            };
            for (const auto& c : cases) {
                if (ECS::Chat::TauntOf(c.text) != c.taunt) verdict = "a taunt was misread";
            }
            Order(sim, Core::ChatCommand{0, 0, "30", 2});
            line = Last(sim);
            if (ok() && (line == nullptr || line->taunt != 30 || line->text != "30")) {
                verdict = "a taunt sent was not logged as one";
            }
        }
        // Refusals, and the long line cut.
        if (ok()) {
            const size_t before = Lines(sim);
            Order(sim, Core::ChatCommand{0, 0, "   ", 1});
            Order(sim, Core::ChatCommand{2, 1, "me", 1});
            Order(sim, Core::ChatCommand{2, 0, "Gaia", 1});
            Order(sim, Core::ChatCommand{7, 0, "who", 1});
            if (Lines(sim) != before) verdict = "an empty or misaddressed message was logged";
            Order(sim, Core::ChatCommand{0, 0, std::string(100, 'x'), 1});
            line = Last(sim);
            if (ok() && (line == nullptr || line->text.size() != ECS::Chat::kMostCharacters)) {
                verdict = "a long message was not cut to 65";
            }
        }
        if (ok()) {
            Order(sim, Core::ResignCommand{3});
            const size_t before = Lines(sim);
            Order(sim, Core::ChatCommand{0, 0, "I quit", 3});
            if (Lines(sim) != before) verdict = "a defeated player chatted";
        }

        // A flare: the sender and its allies, for seven seconds.
        if (ok()) {
            const auto* flares = ECS::Flares::Read(sim.GetRegistry());
            Order(sim, Core::FlareCommand{Fixed32(40), Fixed32(50), 1});
            if (flares == nullptr || flares->flares.empty()) {
                verdict = "a flare went up unlogged";
            } else {
                const auto& flare = flares->flares.back();
                const uint64_t up = flare.tick;
                if (flare.to != Players({1, 2})) {
                    verdict = "a flare was not shown to just the sender and its allies";
                } else if (flare.at.x != Fixed32(40) || flare.at.y != Fixed32(50)) {
                    verdict = "a flare went up somewhere else";
                } else if (flares->lifetime_ms != 7000) {
                    verdict = "a flare's life is not the dat's seven seconds";
                } else if (!ECS::Flares::Showing(*flares, flare, up + 437) ||
                           ECS::Flares::Showing(*flares, flare, up + 438)) {
                    verdict = "a flare did not show for exactly its life";
                }
            }
            const size_t count = flares != nullptr ? flares->flares.size() : 0;
            Order(sim, Core::FlareCommand{Fixed32(40), Fixed32(50), 3});
            if (ok() && flares->flares.size() != count) verdict = "a defeated player sent a flare";
        }
    }

    // Teams locked: no private word to a non-ally, though to everyone and to
    // an ally it goes.
    if (ok()) {
        ECS::Simulation sim;
        sim.Init(ThreePlayers(true));
        const size_t before = Lines(sim);
        Order(sim, Core::ChatCommand{2, 3, "psst", 1});
        if (Lines(sim) != before) {
            verdict = "a private message went to a non-ally with teams locked";
        } else {
            Order(sim, Core::ChatCommand{2, 2, "psst", 1});
            Order(sim, Core::ChatCommand{0, 0, "hello all", 1});
            if (Lines(sim) != before + 2) verdict = "with teams locked, a message to an ally or to all did not go";
        }
    }

    std::printf("chat=%s\n", verdict);
    return ok();
}

} // namespace Verify
