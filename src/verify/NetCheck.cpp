#include "verify/Harness.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/GameSetup.h"
#include "ecs/Digest.h"
#include "ecs/Orders.h"
#include "ecs/Simulation.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "net/Lockstep.h"
#include "net/Wire.h"
#include "save/Save.h"

namespace Verify {

namespace {

entt::entity VillagerOf(const entt::registry& reg, Core::PlayerId player) {
    auto view = reg.view<const ECS::Components::Worker, const ECS::Components::Owner>();
    for (auto e : view) {
        if (view.get<const ECS::Components::Owner>(e).player_id == player) return e;
    }
    return entt::null;
}

bool Moved(const entt::registry& reg, entt::entity unit, const ECS::Components::Vector2D& from) {
    const auto& at = reg.get<ECS::Components::Transform>(unit).position;
    return Core::FixedMath::Distance(at.x - from.x, at.y - from.y) > Core::Fixed32(2);
}

// One machine of the pair: its simulation and its session.
struct Machine {
    ECS::Simulation sim;
    Net::Session session;
    std::vector<Core::Command> due;

    // Runs as many ticks as its turns allow, up to `ticks`.
    int Advance(int ticks) {
        int ran = 0;
        while (ran < ticks && Net::NextTick(session, due)) {
            for (const auto& c : due) ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(), c);
            sim.Update(16);
            Net::TickDone(session, sim.GetRegistry());
            ++ran;
        }
        return ran;
    }
};

} // namespace

// Two machines in lockstep over a real socket on this one: a host and a client.
// Each player's orders have to be carried out on both machines on the same
// tick, the two worlds have to stay identical turn after turn, and when one
// is tampered with, both have to notice.
bool CheckNet() {
    const char* verdict = "ok";
    auto ok = [&]() { return std::strcmp(verdict, "ok") == 0; };

    // Every command survives the wire unchanged.
    {
        const std::vector<Core::Command> samples = {
            Core::MoveCommand{static_cast<entt::entity>(7), Core::Fixed32(3), Core::Fixed32(5) / Core::Fixed32(2), 1},
            Core::FormationMoveCommand{{static_cast<entt::entity>(1), static_cast<entt::entity>(9)},
                                       Core::Fixed32(10), Core::Fixed32(11), Core::Formation::Box, 2},
            Core::BuildCommand{static_cast<entt::entity>(4), 70, 12, 13, 1},
            Core::RelicCommand{static_cast<entt::entity>(2), static_cast<entt::entity>(3), 2},
            Core::UnloadCommand{static_cast<entt::entity>(5), Core::Fixed32(40), Core::Fixed32(7) / Core::Fixed32(4), 1},
            Core::MarketCommand{static_cast<entt::entity>(6), 2, 5, true, 1},
            Core::TradeCommand{static_cast<entt::entity>(8), static_cast<entt::entity>(12), 2},
            Core::TributeCommand{2, 3, 450, 1},
            Core::DiplomacyCommand{3, 1, 2},
            Core::AlliedVictoryCommand{true, 4},
            Core::ChatCommand{2, 3, "Wololo, my friend", 1},
            Core::FlareCommand{Core::Fixed32(33) / Core::Fixed32(2), Core::Fixed32(70), 2},
            Core::ComputerResourceCommand{2, 500, 2},
        };
        for (const auto& c : samples) {
            Net::Wire::Writer out;
            Net::Wire::Write(out, c);
            Net::Wire::Reader in(out.bytes);
            Core::Command back;
            if (!Net::Wire::Read(in, back) || back.index() != c.index()) {
                verdict = "a command did not survive the wire";
                break;
            }
            Net::Wire::Writer again;
            Net::Wire::Write(again, back);
            if (again.bytes != out.bytes) {
                verdict = "a command came back from the wire changed";
                break;
            }
        }
    }

    // The lobby survives the wire, the computer players' difficulty and the
    // team lock with it: every machine loads the same rules for them.
    if (ok()) {
        Core::GameSetup lobby = Core::DefaultSetup();
        lobby.difficulty = 0;
        lobby.lock_teams = false;
        lobby.players[2].computer = true;
        Net::Wire::Writer out;
        Net::Wire::Write(out, lobby);
        Net::Wire::Reader in(out.bytes);
        Core::GameSetup back;
        if (!Net::Wire::Read(in, back) || back.difficulty != 0 || back.lock_teams || !back.players[2].computer) {
            verdict = "the lobby came back from the wire changed";
        }
    }

    Machine host;
    Machine client;
    Core::GameSetup setup = Core::DefaultSetup();
    if (ok() && !Net::Host(host.session, 0, setup, 2)) verdict = "could not open a game";
    const uint16_t port = ok() ? HAL::Net::BoundPort(host.session.listener) : 0;
    if (ok() && !Net::Join(client.session, "127.0.0.1", port)) verdict = "could not join the game";

    // The lobby: welcomed, seated, started.
    for (int i = 0; i < 400 && ok() && !(host.session.started && client.session.started); ++i) {
        Net::Pump(host.session);
        Net::Pump(client.session);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (ok() && !(host.session.started && client.session.started)) verdict = "the game never started";
    if (ok() && client.session.seat != 2) verdict = "the client was not given the second seat";
    if (ok() && client.session.setup.seed != setup.seed) verdict = "the client was given another game";

    if (ok()) {
        host.sim.Init(setup);
        client.sim.Init(client.session.setup);
        if (ECS::Digest::Of(host.sim.GetRegistry()) != ECS::Digest::Of(client.sim.GetRegistry())) {
            verdict = "the two machines built different worlds";
        }
    }

    // Each side orders one of its villagers away; both orders must happen on
    // both machines.
    // Each villager walks a few tiles to open ground beside it.
    auto open_beside = [](const Physics::Grid& grid, const ECS::Components::Vector2D& at) {
        static const int kWays[8][2] = {{4, 0}, {-4, 0}, {0, 4}, {0, -4}, {3, 3}, {-3, -3}, {3, -3}, {-3, 3}};
        for (const auto& way : kWays) {
            const ECS::Components::Vector2D to{at.x + Core::Fixed32(way[0]), at.y + Core::Fixed32(way[1])};
            if (grid.IsWalkable(Physics::TileOf(to.x), Physics::TileOf(to.y))) return to;
        }
        return at;
    };
    entt::entity ours = entt::null;
    entt::entity theirs = entt::null;
    ECS::Components::Vector2D ours_from{};
    ECS::Components::Vector2D theirs_from{};
    if (ok()) {
        ours = VillagerOf(host.sim.GetRegistry(), 1);
        theirs = VillagerOf(client.sim.GetRegistry(), 2);
        ours_from = host.sim.GetRegistry().get<ECS::Components::Transform>(ours).position;
        theirs_from = client.sim.GetRegistry().get<ECS::Components::Transform>(theirs).position;
        const auto a = open_beside(host.sim.GetGrid(), ours_from);
        const auto b = open_beside(client.sim.GetGrid(), theirs_from);
        Net::Issue(host.session, Core::MoveCommand{ours, a.x, a.y, 1});
        Net::Issue(client.session, Core::MoveCommand{theirs, b.x, b.y, 2});
        // A client may not order the host's units: the order is refused on
        // both machines alike.
        Net::Issue(client.session, Core::MoveCommand{ours, ours_from.x - Core::Fixed32(9), ours_from.y, 2});
    }
    int ticks = 0;
    for (int frame = 0; frame < 4000 && ok() && ticks < 600; ++frame) {
        Net::Pump(host.session);
        Net::Pump(client.session);
        ticks += host.Advance(8);
        client.Advance(8);
        if (!host.session.failure.empty() || !client.session.failure.empty()) {
            verdict = "a connection failed during play";
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (ok() && ticks < 600) verdict = "lockstep stalled";
    if (ok()) {
        // Catch the client up to the same tick.
        for (int i = 0; i < 400 && host.session.turn != client.session.turn; ++i) {
            Net::Pump(host.session);
            Net::Pump(client.session);
            client.Advance(1);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    if (ok()) {
        const auto& h = host.sim.GetRegistry();
        const auto& c = client.sim.GetRegistry();
        if (host.session.desynced || client.session.desynced) {
            verdict = "two machines playing the same orders went out of step";
        } else if (!Moved(h, ours, ours_from) || !Moved(c, ours, ours_from)) {
            verdict = "the host's order was not carried out on both machines";
        } else if (h.get<ECS::Components::Transform>(ours).position.x < ours_from.x - Core::Fixed32(5)) {
            verdict = "a client's order for the host's unit was obeyed";
        } else if (!Moved(h, theirs, theirs_from) || !Moved(c, theirs, theirs_from)) {
            verdict = "the client's order was not carried out on both machines";
        } else if (host.session.turn == client.session.turn &&
                   ECS::Digest::Of(h) != ECS::Digest::Of(c)) {
            verdict = "the two worlds differ at the same turn";
        }
    }

    // Create (Restore): the game saved here hosted again; the client is
    // handed the save with its seat, both play on from it, and their worlds
    // stay one.
    if (ok()) {
        Machine host2;
        Machine client2;
        Core::GameSetup saved;
        if (!Save::Write(host.sim, host2.session.restore) || !Save::ReadSetup(host2.session.restore, saved)) {
            verdict = "a saved game gave no lobby to restore";
        }
        if (ok() && !Net::Host(host2.session, 0, saved, 2)) verdict = "could not host a restored game";
        if (ok() && host2.session.restore.empty()) verdict = "hosting dropped the game to restore";
        const uint16_t port2 = ok() ? HAL::Net::BoundPort(host2.session.listener) : 0;
        if (ok() && !Net::Join(client2.session, "127.0.0.1", port2)) verdict = "could not join a restored game";
        for (int i = 0; i < 400 && ok() && !(host2.session.started && client2.session.started); ++i) {
            Net::Pump(host2.session);
            Net::Pump(client2.session);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (ok() && client2.session.restore != host2.session.restore) {
            verdict = "the client was not handed the saved game";
        }
        if (ok()) {
            host2.sim.Init(saved);
            client2.sim.Init(client2.session.setup);
            if (!Save::Read(host2.sim, host2.session.restore) || !Save::Read(client2.sim, client2.session.restore)) {
                verdict = "a restored game would not load";
            } else if (ECS::Digest::Of(host2.sim.GetRegistry()) != ECS::Digest::Of(host.sim.GetRegistry()) ||
                       ECS::Digest::Of(client2.sim.GetRegistry()) != ECS::Digest::Of(host.sim.GetRegistry())) {
                verdict = "a restored game is not the game saved";
            }
        }
        int played = 0;
        for (int frame = 0; frame < 4000 && ok() && played < 200; ++frame) {
            Net::Pump(host2.session);
            Net::Pump(client2.session);
            played += host2.Advance(8);
            client2.Advance(8);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (ok() && played < 200) verdict = "a restored game stalled";
        if (ok() && (host2.session.desynced || client2.session.desynced)) {
            verdict = "a restored game went out of step";
        }
        Net::Leave(host2.session);
        Net::Leave(client2.session);
    }

    // A machine that falls silent holds the others back: the host may run no
    // further ahead than the turns already in hand, or it would be playing
    // turns without the client's orders in them.
    if (ok()) {
        const uint32_t from = host.session.turn;
        for (int frame = 0; frame < 200; ++frame) {
            Net::Pump(host.session);
            host.Advance(8);
        }
        if (host.session.turn > from + Net::kDelayTurns + 1) {
            verdict = "the host ran ahead of a silent client";
        }
        // The client speaks again, and both carry on.
        for (int i = 0; i < 2000 && host.session.turn < from + 20; ++i) {
            Net::Pump(host.session);
            Net::Pump(client.session);
            host.Advance(8);
            client.Advance(8);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (ok() && host.session.turn < from + 20) verdict = "the game did not resume after a pause";
    }

    // Tampering with one machine's world is noticed by both.
    if (ok()) {
        client.sim.GetRegistry().get<ECS::Components::Transform>(theirs).position.y =
            client.sim.GetRegistry().get<ECS::Components::Transform>(theirs).position.y + Core::Fixed32(1);
        for (int frame = 0; frame < 2000 && !(host.session.desynced && client.session.desynced); ++frame) {
            Net::Pump(host.session);
            Net::Pump(client.session);
            host.Advance(8);
            client.Advance(8);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (!host.session.desynced || !client.session.desynced) {
            verdict = "a machine whose world was changed was not caught out of step";
        }
    }
    Net::Leave(host.session);
    Net::Leave(client.session);
    std::printf("net=%s\n", verdict);
    return ok();
}

} // namespace Verify
