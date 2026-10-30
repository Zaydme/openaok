#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net/Lockstep.h"

// A network game's lobby, and finding games to join.
//
// The host keeps the table (Net::Table). A person who joins takes the first
// open row; the host chooses the game's settings and what the other rows are:
// open to a person, closed, or a computer player. Each person changes only
// their own row -- civilisation, player number, team, ready -- by asking the
// host, which sends the whole table to everyone after every change. Chat goes
// the same way. Once everyone is ready the host starts the game: the rows in
// use become the setup's seats in order, and each person's seat goes to them
// with the setup (Net::Launch).
//
// A host answers anyone looking for games with a datagram on kPort: a query
// broadcast on the local network, or sent to one machine.
namespace Net {

// The game's port, for its connections and for finding games.
constexpr uint16_t kPort = 27015;
// The lobby's language: machines speaking another cannot play together.
constexpr uint8_t kVersion = 1;

// Opens a lobby on `port` for `name`, in the first row. `settings` are the
// game's; a restored game's seats are its own and fixed.
bool OpenLobby(Session& session, uint16_t port, const Core::GameSetup& settings, const std::string& name,
               bool restoring);
// Joins the lobby at `host`:`port` as `name`. The connection is made in the
// background: Pump until `session.lobby`, a failure, or `refused`.
bool JoinLobby(Session& session, const std::string& host, uint16_t port, const std::string& name);
// Whether a lobby being joined has been reached and has seated this machine.
bool Seated(const Session& session);

// The host: the game's settings, and what row `row` is (open, closed or a
// computer player) -- a person's row is theirs.
void SetSettings(Session& session, const Core::GameSetup& settings);
void SetRowKind(Session& session, int row, RowKind kind);
// A row's civilisation, player number and team: this machine's own, or the
// host's computer players'.
void SetRow(Session& session, int row, uint8_t civ, uint8_t number, uint8_t team);
// This machine's I'm Ready!
void SetReady(Session& session, bool ready);
void Say(Session& session, const std::string& text);

// Why the host cannot start yet: a LANGUAGE.DLL message, 0 when it can.
int StartRefusal(const Session& session);
// The host: starts the game from the table. `seed` draws the random
// civilisations and the map.
bool StartGame(Session& session, uint32_t seed);

// The setup the table describes: its rows in use as seats in order.
Core::GameSetup SetupOf(const Session& session, uint32_t seed);

// Handles a lobby message; false when it is not one.
bool LobbyMessage(Session& session, const std::vector<uint8_t>& message, Peer* from);
// The host's part of the lobby each frame: seating those who connect,
// pinging, answering those looking for games.
void ServeLobby(Session& session);
// A connection lost in the lobby: a client's row opens again; the host gone
// cancels the game.
void LostInLobby(Session& session, Peer& peer);

// Leaves the lobby; the host leaving cancels the game for everyone.
void LeaveLobby(Session& session);

// A game found.
struct Found {
    std::string name;       // the host's name
    std::string address;
    uint16_t port = 0;
    uint8_t people = 0;     // seated so far
    uint8_t open = 0;       // rows still open
    bool restoring = false;
    bool same_version = true;
    uint32_t heard = 0;     // when it last answered, in milliseconds
};

// Looking for games: the games heard of lately, forgotten once they fall
// silent.
struct Finder {
    HAL::Net::Socket socket = HAL::Net::kNoSocket;
    std::vector<Found> games;
};

bool OpenFinder(Finder& finder);
// Asks every host on the local network, or the one at `host`.
void Look(Finder& finder, const std::string& host);
// Takes the answers in, and forgets games not heard from for a while.
void Listen(Finder& finder, uint32_t now_ms);
void CloseFinder(Finder& finder);

} // namespace Net
