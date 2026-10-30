#include "net/Lobby.h"

#include <algorithm>
#include <chrono>

#include "net/Wire.h"

namespace Net {

namespace {

// Message kinds: a client's hello, its row, chat; the host's table, a ping
// and its answer, the game canceled, a client put out.
constexpr uint8_t kHello = 'H';
constexpr uint8_t kRowAsk = 'R';
constexpr uint8_t kChat = 'C';
constexpr uint8_t kTable = 'L';
constexpr uint8_t kPing = 'P';
constexpr uint8_t kPong = 'p';
constexpr uint8_t kCancel = 'X';
constexpr uint8_t kEject = 'E';

// What the datagrams that find games begin with.
constexpr uint8_t kQuery[4] = {'A', 'o', 'K', '?'};
constexpr uint8_t kAnswer[4] = {'A', 'o', 'K', '!'};

// The civilisations a row may play, and the teams.
constexpr int kCivs = 13;
constexpr int kTeams = 4;
// How often the host pings each client; how long a game found stays listed
// without answering again; how much chat is kept.
constexpr uint32_t kPingMs = 1000;
constexpr uint32_t kForgetMs = 3000;
constexpr size_t kChatKept = 100;

// LANGUAGE.DLL: why the host cannot start, and why a client was put out.
constexpr int kAllReady = 9665;
constexpr int kOnlyOne = 9670;
constexpr int kDropped = 9673;
constexpr int kWrongVersion = 9699;

uint32_t Clock() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool InUse(const Row& row) { return row.kind == RowKind::Person || row.kind == RowKind::Computer; }

// The host: whether a person's row has its person -- the host's own, or a
// client's.
bool Claimed(const Session& session, int row) {
    if (row == 0) return true;
    for (const Peer& peer : session.peers) {
        if (peer.row == row) return true;
    }
    return false;
}

std::vector<uint8_t> TableFor(const Session& session, int row) {
    Wire::Writer out;
    out.U8(kTable);
    out.U8(static_cast<uint8_t>(row));
    out.U8(session.table.restoring ? 1 : 0);
    Wire::Write(out, session.setup);
    for (const Row& r : session.table.rows) {
        out.U8(static_cast<uint8_t>(r.kind));
        out.Text(r.name);
        out.U8(r.civ);
        out.U8(r.number);
        out.U8(r.team);
        out.U8(r.ready ? 1 : 0);
        out.U16(r.ping);
    }
    return out.bytes;
}

// The host: the table to everyone after a change.
void Changed(Session& session) {
    ++session.table.changes;
    for (Peer& peer : session.peers) {
        if (peer.row >= 0) HAL::Net::Send(peer.connection, TableFor(session, peer.row));
    }
}

void Heard(Session& session, const std::string& name, const std::string& text) {
    session.table.chat.push_back({name, text});
    if (session.table.chat.size() > kChatKept) session.table.chat.erase(session.table.chat.begin());
    ++session.table.changes;
}

// The host: a line said, to everyone.
void Spread(Session& session, const std::string& name, const std::string& text) {
    Heard(session, name, text);
    Wire::Writer out;
    out.U8(kChat);
    out.Text(name);
    out.Text(text);
    for (Peer& peer : session.peers) {
        if (peer.row >= 0) HAL::Net::Send(peer.connection, out.bytes);
    }
}

void Eject(Peer& peer, int why) {
    Wire::Writer out;
    out.U8(kEject);
    out.U16(static_cast<uint16_t>(why));
    HAL::Net::Send(peer.connection, out.bytes);
    HAL::Net::Pump(peer.connection);
    HAL::Net::Close(peer.connection);
    peer.row = -1;
}

// The host: the row a person saying hello takes -- a restored game's row in
// their name, or the first still to be taken; the first open row otherwise.
int RowFor(Session& session, const std::string& name) {
    if (session.table.restoring) {
        for (int r = 1; r < kRows; ++r) {
            const Row& row = session.table.rows[r];
            if (row.kind == RowKind::Person && !Claimed(session, r) && row.name == name) return r;
        }
        for (int r = 1; r < kRows; ++r) {
            if (session.table.rows[r].kind == RowKind::Person && !Claimed(session, r)) return r;
        }
        return -1;
    }
    for (int r = 1; r < kRows; ++r) {
        if (session.table.rows[r].kind == RowKind::Open) return r;
    }
    return -1;
}

void Answer(Session& session) {
    std::vector<uint8_t> query;
    std::string from;
    while (HAL::Net::ReceiveDatagram(session.beacon, query, from)) {
        if (query.size() < 4 || !std::equal(kQuery, kQuery + 4, query.begin())) continue;
        uint8_t people = 0, open = 0;
        for (int r = 0; r < kRows; ++r) {
            const Row& row = session.table.rows[r];
            if (row.kind == RowKind::Person && Claimed(session, r)) ++people;
            if (row.kind == RowKind::Open || (row.kind == RowKind::Person && !Claimed(session, r))) ++open;
        }
        Wire::Writer out;
        out.bytes.assign(kAnswer, kAnswer + 4);
        out.U8(kVersion);
        out.Text(session.name);
        out.U16(HAL::Net::BoundPort(session.listener));
        out.U8(people);
        out.U8(open);
        out.U8(session.table.restoring ? 1 : 0);
        const uint16_t port = query.size() >= 7 ? static_cast<uint16_t>(query[5] | (query[6] << 8)) : kPort;
        HAL::Net::SendDatagram(session.beacon, from, port, out.bytes);
    }
}

} // namespace

bool OpenLobby(Session& session, uint16_t port, const Core::GameSetup& settings, const std::string& name,
               bool restoring) {
    std::vector<uint8_t> restore = std::move(session.restore);
    session = Session{};
    session.restore = std::move(restore);
    session.role = Role::Host;
    session.lobby = true;
    session.seat = 1;
    session.name = name;
    session.setup = settings;
    session.table.restoring = restoring;
    if (restoring) {
        // The saved game's seats, in order, each a row.
        int r = 0;
        for (int seat = 1; seat < Core::kMaxPlayers && r < kRows; ++seat) {
            const Core::PlayerSetup& p = settings.players[seat];
            if (!p.active) continue;
            Row& row = session.table.rows[r++];
            row.kind = p.computer ? RowKind::Computer : RowKind::Person;
            row.name = p.name;
            row.civ = static_cast<uint8_t>(p.civ);
            row.number = static_cast<uint8_t>(p.colour >= 0 ? p.colour : seat - 1);
            row.team = p.team;
        }
        for (; r < kRows; ++r) session.table.rows[r].kind = RowKind::Closed;
        session.table.rows[0].name = name;
    } else {
        for (int r = 0; r < kRows; ++r) session.table.rows[r].number = static_cast<uint8_t>(r);
        session.table.rows[0].kind = RowKind::Person;
        session.table.rows[0].name = name;
    }
    session.listener = HAL::Net::Listen(port);
    if (session.listener == HAL::Net::kNoSocket) {
        session.failure = "cannot listen on that port";
        return false;
    }
    session.beacon = HAL::Net::OpenDatagram(kPort);
    return true;
}

bool JoinLobby(Session& session, const std::string& host, uint16_t port, const std::string& name) {
    session = Session{};
    session.role = Role::Client;
    session.name = name;
    Peer peer;
    peer.seat = 1;
    if (!HAL::Net::Dial(host, port, peer.connection)) {
        session.failure = "cannot reach the host";
        return false;
    }
    session.dialing = true;
    session.peers.push_back(std::move(peer));
    return true;
}

bool Seated(const Session& session) { return session.lobby && !session.dialing; }

void SetSettings(Session& session, const Core::GameSetup& settings) {
    if (session.role != Role::Host || session.table.restoring) return;
    session.setup = settings;
    Changed(session);
}

void SetRowKind(Session& session, int row, RowKind kind) {
    if (session.role != Role::Host || session.table.restoring || row <= 0 || row >= kRows) return;
    if (kind == RowKind::Person || Claimed(session, row)) return;
    Row& r = session.table.rows[row];
    r.kind = kind;
    r.name.clear();
    r.ready = false;
    Changed(session);
}

void SetRow(Session& session, int row, uint8_t civ, uint8_t number, uint8_t team) {
    if (row < 0 || row >= kRows || session.table.restoring) return;
    civ = static_cast<uint8_t>(std::min<int>(civ, kCivs));
    number = static_cast<uint8_t>(std::min<int>(number, kRows - 1));
    team = static_cast<uint8_t>(std::min<int>(team, kTeams));
    Row& r = session.table.rows[row];
    if (session.role == Role::Host) {
        if (row != 0 && r.kind != RowKind::Computer) return;
        r.civ = civ;
        r.number = number;
        r.team = team;
        Changed(session);
    } else if (row == session.row && !session.peers.empty()) {
        r.civ = civ;
        r.number = number;
        r.team = team;
        Wire::Writer out;
        out.U8(kRowAsk);
        out.U8(civ);
        out.U8(number);
        out.U8(team);
        out.U8(r.ready ? 1 : 0);
        HAL::Net::Send(session.peers[0].connection, out.bytes);
    }
}

void SetReady(Session& session, bool ready) {
    Row& r = session.table.rows[session.row];
    r.ready = ready;
    if (session.role == Role::Host) {
        Changed(session);
    } else if (!session.peers.empty()) {
        Wire::Writer out;
        out.U8(kRowAsk);
        out.U8(r.civ);
        out.U8(r.number);
        out.U8(r.team);
        out.U8(ready ? 1 : 0);
        HAL::Net::Send(session.peers[0].connection, out.bytes);
    }
}

void Say(Session& session, const std::string& text) {
    if (text.empty()) return;
    if (session.role == Role::Host) {
        Spread(session, session.name, text);
    } else if (!session.peers.empty()) {
        Wire::Writer out;
        out.U8(kChat);
        out.Text(text);
        HAL::Net::Send(session.peers[0].connection, out.bytes);
    }
}

int StartRefusal(const Session& session) {
    int players = 0;
    for (int r = 0; r < kRows; ++r) {
        const Row& row = session.table.rows[r];
        if (!InUse(row)) continue;
        ++players;
        if (row.kind == RowKind::Person && (!Claimed(session, r) || !row.ready)) return kAllReady;
    }
    return players < 2 ? kOnlyOne : 0;
}

Core::GameSetup SetupOf(const Session& session, uint32_t seed) {
    Core::GameSetup game = session.setup;
    if (session.table.restoring) return game;
    game.seed = seed;
    uint32_t roll = seed * 2654435761u;
    auto pick = [&](int n) {
        roll = roll * 1664525u + 1013904223u;
        return static_cast<int>((roll >> 8) % static_cast<uint32_t>(n));
    };
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) game.players[seat] = Core::PlayerSetup{};
    int seat = 1;
    for (const Row& row : session.table.rows) {
        if (!InUse(row) || seat >= Core::kMaxPlayers) continue;
        Core::PlayerSetup& p = game.players[seat++];
        p.active = true;
        p.civ = row.civ == 0 ? 1 + pick(kCivs) : row.civ;
        p.team = row.team;
        p.computer = row.kind == RowKind::Computer;
        if (!p.computer) p.name = row.name;
        p.colour = row.number;
    }
    return game;
}

bool StartGame(Session& session, uint32_t seed) {
    if (session.role != Role::Host || StartRefusal(session) != 0) return false;
    session.setup = SetupOf(session, seed);
    // Each row in use is the next seat; a person's seat goes to them.
    int seat_of[kRows] = {};
    int seat = 1;
    for (int r = 0; r < kRows; ++r) {
        if (InUse(session.table.rows[r])) seat_of[r] = seat++;
    }
    session.humans.clear();
    for (int r = 0; r < kRows; ++r) {
        if (session.table.rows[r].kind == RowKind::Person && seat_of[r] > 0) {
            session.humans.push_back(static_cast<Core::PlayerId>(seat_of[r]));
        }
    }
    session.seat = static_cast<Core::PlayerId>(seat_of[0]);
    for (Peer& peer : session.peers) {
        peer.seat = peer.row >= 0 ? static_cast<Core::PlayerId>(seat_of[peer.row]) : 0;
        if (peer.seat == 0) HAL::Net::Close(peer.connection);
    }
    std::erase_if(session.peers, [](const Peer& p) { return p.seat == 0; });
    if (session.beacon != HAL::Net::kNoSocket) {
        HAL::Net::CloseDatagram(session.beacon);
        session.beacon = HAL::Net::kNoSocket;
    }
    Launch(session);
    return true;
}

bool LobbyMessage(Session& session, const std::vector<uint8_t>& message, Peer* from) {
    Wire::Reader in(message);
    const uint8_t kind = in.U8();
    if (session.role == Role::Host && from != nullptr) {
        if (kind == kHello) {
            const uint8_t version = in.U8();
            const std::string name = in.Text();
            if (!in.ok || from->row >= 0) return true;
            if (version != kVersion) {
                Eject(*from, kWrongVersion);
                return true;
            }
            const int row = RowFor(session, name);
            if (row < 0) {
                Eject(*from, kDropped);
                return true;
            }
            from->row = row;
            Row& r = session.table.rows[row];
            r.kind = RowKind::Person;
            r.name = name;
            r.ready = false;
            Changed(session);
            return true;
        }
        if (kind == kRowAsk && from->row >= 0) {
            Row& r = session.table.rows[from->row];
            const uint8_t civ = in.U8(), number = in.U8(), team = in.U8(), ready = in.U8();
            if (!in.ok) return true;
            if (!session.table.restoring) {
                r.civ = static_cast<uint8_t>(std::min<int>(civ, kCivs));
                r.number = static_cast<uint8_t>(std::min<int>(number, kRows - 1));
                r.team = static_cast<uint8_t>(std::min<int>(team, kTeams));
            }
            r.ready = ready != 0;
            Changed(session);
            return true;
        }
        if (kind == kChat && from->row >= 0) {
            const std::string text = in.Text();
            if (in.ok && !text.empty()) Spread(session, session.table.rows[from->row].name, text);
            return true;
        }
        if (kind == kPong && from->row >= 0) {
            const uint32_t sent = in.U32();
            if (in.ok) {
                session.table.rows[from->row].ping = static_cast<uint16_t>(std::min<uint32_t>(Clock() - sent, 9999));
            }
            return true;
        }
        return kind == kHello || kind == kRowAsk || kind == kChat || kind == kPong;
    }
    if (session.role == Role::Client) {
        if (kind == kTable) {
            const int row = in.U8();
            const bool restoring = in.U8() != 0;
            Core::GameSetup settings;
            if (!Wire::Read(in, settings)) {
                session.failure = "a garbled lobby from the host";
                return true;
            }
            Table table;
            table.chat = std::move(session.table.chat);
            table.restoring = restoring;
            for (Row& r : table.rows) {
                r.kind = static_cast<RowKind>(std::min<int>(in.U8(), static_cast<int>(RowKind::Person)));
                r.name = in.Text();
                r.civ = in.U8();
                r.number = in.U8();
                r.team = in.U8();
                r.ready = in.U8() != 0;
                r.ping = in.U16();
            }
            if (!in.ok || row >= kRows) {
                session.failure = "a garbled lobby from the host";
                return true;
            }
            table.changes = session.table.changes + 1;
            session.table = std::move(table);
            session.row = row;
            session.setup = settings;
            session.lobby = true;
            return true;
        }
        if (kind == kChat) {
            const std::string name = in.Text();
            const std::string text = in.Text();
            if (in.ok) Heard(session, name, text);
            return true;
        }
        if (kind == kPing) {
            const uint32_t stamp = in.U32();
            if (in.ok && !session.peers.empty()) {
                Wire::Writer out;
                out.U8(kPong);
                out.U32(stamp);
                HAL::Net::Send(session.peers[0].connection, out.bytes);
            }
            return true;
        }
        if (kind == kCancel) {
            session.canceled = true;
            return true;
        }
        if (kind == kEject) {
            session.ejected = true;
            session.refused = in.U16();
            return true;
        }
    }
    return false;
}

void ServeLobby(Session& session) {
    HAL::Net::Connection incoming;
    while (HAL::Net::Accept(session.listener, incoming)) {
        Peer peer;
        peer.connection = std::move(incoming);
        session.peers.push_back(std::move(peer));
    }
    const uint32_t now = Clock();
    for (Peer& peer : session.peers) {
        if (peer.row < 0 || now - peer.pinged < kPingMs) continue;
        peer.pinged = now;
        Wire::Writer out;
        out.U8(kPing);
        out.U32(now);
        HAL::Net::Send(peer.connection, out.bytes);
    }
    // The table again now and then, for the pings.
    if (now - session.table_sent >= kPingMs) {
        session.table_sent = now;
        Changed(session);
    }
    Answer(session);
}

void LostInLobby(Session& session, Peer& peer) {
    if (session.role == Role::Client) {
        if (!session.ejected) session.canceled = true;
        return;
    }
    if (peer.row >= 0) {
        Row& r = session.table.rows[peer.row];
        if (!session.table.restoring) {
            r = Row{};
            r.number = static_cast<uint8_t>(peer.row);
        }
        r.ready = false;
        peer.row = -1;
        Changed(session);
    }
}

void LeaveLobby(Session& session) {
    if (session.role == Role::Host && !session.started) {
        Wire::Writer out;
        out.U8(kCancel);
        for (Peer& peer : session.peers) {
            HAL::Net::Send(peer.connection, out.bytes);
            HAL::Net::Pump(peer.connection);
        }
    }
    if (session.beacon != HAL::Net::kNoSocket) HAL::Net::CloseDatagram(session.beacon);
    session.beacon = HAL::Net::kNoSocket;
    Leave(session);
}

bool OpenFinder(Finder& finder) {
    finder.games.clear();
    finder.socket = HAL::Net::OpenDatagram(0);
    return finder.socket != HAL::Net::kNoSocket;
}

void Look(Finder& finder, const std::string& host) {
    Wire::Writer out;
    out.bytes.assign(kQuery, kQuery + 4);
    out.U8(kVersion);
    // Where to answer: this socket's port.
    out.U16(HAL::Net::BoundPort(finder.socket));
    HAL::Net::SendDatagram(finder.socket, host, kPort, out.bytes);
}

void Listen(Finder& finder, uint32_t now_ms) {
    std::vector<uint8_t> answer;
    std::string from;
    while (HAL::Net::ReceiveDatagram(finder.socket, answer, from)) {
        if (answer.size() < 4 || !std::equal(kAnswer, kAnswer + 4, answer.begin())) continue;
        Wire::Reader in(answer);
        in.at = 4;
        Found game;
        game.same_version = in.U8() == kVersion;
        game.name = in.Text();
        game.port = in.U16();
        game.people = in.U8();
        game.open = in.U8();
        game.restoring = in.U8() != 0;
        if (!in.ok) continue;
        game.address = from;
        game.heard = now_ms;
        auto same = std::find_if(finder.games.begin(), finder.games.end(), [&](const Found& f) {
            return f.address == game.address && f.port == game.port;
        });
        if (same != finder.games.end()) *same = game;
        else finder.games.push_back(game);
    }
    std::erase_if(finder.games, [&](const Found& f) { return now_ms - f.heard > kForgetMs; });
}

void CloseFinder(Finder& finder) {
    HAL::Net::CloseDatagram(finder.socket);
    finder.socket = HAL::Net::kNoSocket;
    finder.games.clear();
}

} // namespace Net
