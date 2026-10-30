#include "verify/Harness.h"

#include <chrono>
#include <cstdio>
#include <thread>

#include "core/GameSetup.h"
#include "ecs/Orders.h"
#include "ecs/Simulation.h"
#include "net/Lobby.h"
#include "net/Wire.h"

namespace Verify {

namespace {

constexpr int kComputerRow = 2;
constexpr uint8_t kTeutons = 4;
constexpr uint32_t kSeed = 7;

// Pumps both ends until `done`, or about two seconds.
template <typename Done>
bool PumpUntil(Net::Session& a, Net::Session& b, Done done) {
    for (int i = 0; i < 200; ++i) {
        Net::Pump(a);
        Net::Pump(b);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

// A network game's lobby, a host and a client over a socket on this machine:
// the game found by asking the host, the client seated in the first open row
// and shown the host's settings, a row it changes and its I'm Ready! reaching
// everyone, chat heard on both, a computer player put in a row, the host
// refused a start until everyone is ready, a client that leaves giving its
// row back, and the game started from the table on both machines in step.
bool CheckLobby() {
    const char* verdict = nullptr;
    auto ok = [&] { return verdict == nullptr; };
    Core::GameSetup settings = Core::DefaultSetup();
    settings.map_script = "Baltic";
    settings.population_limit = 125;

    Net::Session host, client;
    if (!Net::OpenLobby(host, 0, settings, "Host", false)) verdict = "could not open a lobby";
    const uint16_t port = ok() ? HAL::Net::BoundPort(host.listener) : 0;

    // Found by asking the host's machine.
    Net::Finder finder;
    if (ok() && !Net::OpenFinder(finder)) verdict = "could not look for games";
    if (ok()) {
        bool found = false;
        for (int i = 0; i < 100 && !found; ++i) {
            if (i % 20 == 0) Net::Look(finder, "127.0.0.1");
            Net::Pump(host);
            Net::Listen(finder, static_cast<uint32_t>(i * 10));
            for (const Net::Found& g : finder.games) {
                if (g.name == "Host" && g.port == port && g.people == 1 && g.open == 7) found = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!found) verdict = "the host's game was not found";
        Net::CloseFinder(finder);
    }

    if (ok() && !Net::JoinLobby(client, "127.0.0.1", port, "Guest")) verdict = "could not reach the lobby";
    if (ok() && !PumpUntil(host, client, [&] { return Net::Seated(client) && client.row == 1; })) {
        verdict = "the client was not seated in the first open row";
    }
    if (ok() && (client.setup.map_script != "Baltic" || client.setup.population_limit != 125)) {
        verdict = "the client was not shown the host's settings";
    }
    if (ok() && (client.table.rows[0].name != "Host" || host.table.rows[1].name != "Guest")) {
        verdict = "the two machines do not see the same players";
    }

    // The host's start is refused until everyone is ready.
    if (ok()) {
        Net::SetReady(host, true);
        if (Net::StartRefusal(host) != 9665) verdict = "the host could start before everyone was ready";
    }
    if (ok()) {
        Net::SetRow(client, client.row, kTeutons, 5, 2);
        Net::SetReady(client, true);
        Net::SetRowKind(host, kComputerRow, Net::RowKind::Computer);
        Net::SetRow(host, kComputerRow, 0, 2, 1);
        Net::Say(client, "hello");
        Net::Say(host, "welcome");
        const bool heard = PumpUntil(host, client, [&] {
            return host.table.rows[1].ready && host.table.rows[1].civ == kTeutons &&
                   client.table.rows[kComputerRow].kind == Net::RowKind::Computer &&
                   host.table.chat.size() == 2 && client.table.chat.size() == 2;
        });
        if (!heard) verdict = "a row, the ready check or chat did not reach both machines";
    }
    if (ok()) {
        // In the host's order, the same on both, each line its speaker's.
        bool same = true, guest = false, own = false;
        for (size_t i = 0; i < 2; ++i) {
            const Net::Said& h = host.table.chat[i];
            const Net::Said& c = client.table.chat[i];
            same = same && h.name == c.name && h.text == c.text;
            guest = guest || (h.name == "Guest" && h.text == "hello");
            own = own || (h.name == "Host" && h.text == "welcome");
        }
        if (!same || !guest || !own) verdict = "chat was heard differently or from the wrong player";
    }
    if (ok() && Net::StartRefusal(host) != 0) verdict = "the host could not start with everyone ready";

    // A second client that leaves gives its row back.
    if (ok()) {
        Net::Session other;
        Net::JoinLobby(other, "127.0.0.1", port, "Passer");
        if (!PumpUntil(host, other, [&] { return Net::Seated(other) && host.table.rows[3].name == "Passer"; })) {
            verdict = "a second client was not seated";
        }
        Net::Leave(other);
        if (ok() && !PumpUntil(host, client, [&] { return host.table.rows[3].kind == Net::RowKind::Open; })) {
            verdict = "a client that left kept its row";
        }
    }

    // The game, from the table, on both machines.
    if (ok() && !Net::StartGame(host, kSeed)) verdict = "the host could not start the game";
    if (ok() && !PumpUntil(host, client, [&] { return client.started; })) verdict = "the client's game never started";
    if (ok()) {
        Net::Wire::Writer a, b;
        Net::Wire::Write(a, host.setup);
        Net::Wire::Write(b, client.setup);
        const Core::PlayerSetup& guest = host.setup.players[2];
        if (a.bytes != b.bytes) verdict = "the two machines start different games";
        else if (client.seat != 2 || host.seat != 1) verdict = "the people were not given their seats";
        else if (!guest.active || guest.computer || guest.civ != kTeutons || guest.colour != 5 || guest.team != 2) {
            verdict = "the client's row did not become its seat";
        } else if (!host.setup.players[3].active || !host.setup.players[3].computer || host.setup.players[3].team != 1) {
            verdict = "the computer player's row did not become a seat";
        } else if (host.setup.players[4].active) {
            verdict = "an open row became a seat";
        }
    }
    if (ok()) {
        ECS::Simulation a, b;
        a.Init(host.setup);
        b.Init(client.setup);
        std::vector<Core::Command> due;
        int ran = 0;
        for (int i = 0; i < 4000 && ran < 200; ++i) {
            Net::Pump(host);
            Net::Pump(client);
            while (Net::NextTick(host, due)) {
                for (const auto& c : due) ECS::Orders::Execute(a.GetRegistry(), a.GetGrid(), c);
                a.Update(16);
                Net::TickDone(host, a.GetRegistry());
                ++ran;
            }
            while (Net::NextTick(client, due)) {
                for (const auto& c : due) ECS::Orders::Execute(b.GetRegistry(), b.GetGrid(), c);
                b.Update(16);
                Net::TickDone(client, b.GetRegistry());
            }
            if (ran < 200) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (ran < 200) verdict = "the game started from the lobby stalled";
        else if (host.desynced || client.desynced) verdict = "the game started from the lobby went out of step";
    }
    Net::Leave(host);
    Net::Leave(client);
    std::printf("lobby=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
