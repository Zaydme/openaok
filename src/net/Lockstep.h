#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/Command.h"
#include "core/GameSetup.h"
#include "hal/Net.h"

// A network game, in lockstep.
//
// Every machine runs the whole simulation; what travels between them is only
// what the players order. Time is cut into turns of a few ticks. An order given
// during turn T is sent at once and carried out at turn T + kDelayTurns by
// every machine, in seat order -- so each has a couple of turns to arrive, and
// a tick may only run once every player's orders for its turn are in. Nothing
// else need agree, because the simulation is deterministic fixed point: same
// orders, same ticks, same world.
//
// Each turn's orders also carry the sender's digest of the world at the end of
// the turn before (ecs/Digest). The first turn on which two digests differ is
// a desync, and the game says so instead of playing on in two different worlds.
//
// The host relays: clients send their orders to it and it passes them on.
// The lobby is the host's: it seats each client as it connects, sends it the
// game's setup, and starts everyone once the table is full.
namespace Net {

constexpr int kTicksPerTurn = 8;
constexpr uint32_t kDelayTurns = 2;

enum class Role : uint8_t { Offline, Host, Client };

struct Peer {
    HAL::Net::Connection connection;
    Core::PlayerId seat = 0;
};

struct Session {
    Role role = Role::Offline;
    Core::PlayerId seat = 0;               // this machine's
    Core::GameSetup setup;
    // A restored game: the saved game every machine starts from, which the
    // host hands each client with its seat. Empty for a new game.
    std::vector<uint8_t> restore;
    std::vector<Core::PlayerId> humans;    // seats played from a keyboard
    int expected = 0;                      // the host: people it waits for, itself included
    HAL::Net::Socket listener = HAL::Net::kNoSocket;
    std::vector<Peer> peers;               // the host: one per client; a client: the host
    bool welcomed = false;
    bool started = false;

    uint32_t turn = 0;                     // being played
    int tick_in_turn = 0;
    // Orders per turn, per seat; a seat's entry exists once its orders for
    // that turn -- possibly none -- have arrived.
    std::map<uint32_t, std::map<Core::PlayerId, std::vector<Core::Command>>> inbox;
    std::map<uint32_t, std::map<Core::PlayerId, uint64_t>> digests;
    std::vector<Core::Command> outbox;     // given since the last send

    bool desynced = false;
    uint32_t desync_turn = 0;
    // Found here and not yet told to the others: a machine that stops on a
    // desync stops sending its turns, so the others would never see it.
    bool announce_desync = false;
    std::string failure;                   // a dropped connection, a bad message
};

// Opens a game on `port` for `people` players at keyboards, this machine
// sitting at seat 1. The setup says who plays which seat.
bool Host(Session& session, uint16_t port, const Core::GameSetup& setup, int people);

// Joins the game at `host`:`port`. The seat and the setup come from the host.
bool Join(Session& session, const std::string& host, uint16_t port);

// Moves messages: the lobby, relaying, and orders arriving.
void Pump(Session& session);

// An order given at this machine, for the next turn it can still reach.
void Issue(Session& session, const Core::Command& command);

// Whether the next tick may run. At the first tick of a turn `due` receives
// everyone's orders for it, in seat order, to be carried out before the tick.
bool NextTick(Session& session, std::vector<Core::Command>& due);

// After each tick: at the end of a turn, records the world's digest for it.
void TickDone(Session& session, const entt::registry& registry);

void Leave(Session& session);

} // namespace Net
