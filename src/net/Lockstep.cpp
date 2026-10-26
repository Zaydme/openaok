#include "net/Lockstep.h"

#include <algorithm>

#include "ecs/Digest.h"
#include "net/Wire.h"

namespace Net {

namespace {

// Message kinds.
constexpr uint8_t kWelcome = 'W';
constexpr uint8_t kStart = 'S';
constexpr uint8_t kTurn = 'T';
constexpr uint8_t kDesync = 'D';

// No digest yet: the first turns have no turn before them.
constexpr uint32_t kNoDigest = 0xFFFFFFFFu;

std::vector<uint8_t> Welcome(const Session& session, Core::PlayerId seat) {
    Wire::Writer out;
    out.U8(kWelcome);
    out.U8(seat);
    Wire::Write(out, session.setup);
    out.U32(static_cast<uint32_t>(session.restore.size()));
    out.bytes.insert(out.bytes.end(), session.restore.begin(), session.restore.end());
    return out.bytes;
}

std::vector<uint8_t> Start() {
    Wire::Writer out;
    out.U8(kStart);
    return out.bytes;
}

std::vector<uint8_t> Turn(Core::PlayerId seat, uint32_t turn, uint32_t digest_turn, uint64_t digest,
                          const std::vector<Core::Command>& commands) {
    Wire::Writer out;
    out.U8(kTurn);
    out.U8(seat);
    out.U32(turn);
    out.U32(digest_turn);
    out.U64(digest);
    out.U16(static_cast<uint16_t>(commands.size()));
    for (const Core::Command& c : commands) Wire::Write(out, c);
    return out.bytes;
}

void Broadcast(Session& session, const std::vector<uint8_t>& message, Core::PlayerId except) {
    for (Peer& peer : session.peers) {
        if (peer.seat != except) HAL::Net::Send(peer.connection, message);
    }
}

// The first turns have nothing ordered in them; every seat's empty orders are
// taken as given.
void Begin(Session& session) {
    session.started = true;
    for (uint32_t t = 0; t < kDelayTurns; ++t) {
        for (Core::PlayerId seat : session.humans) session.inbox[t][seat];
    }
}

void Compare(Session& session, uint32_t turn) {
    const auto found = session.digests.find(turn);
    if (found == session.digests.end() || found->second.size() < 2) return;
    const uint64_t first = found->second.begin()->second;
    for (const auto& [seat, digest] : found->second) {
        if (digest != first && !session.desynced) {
            session.desynced = true;
            session.desync_turn = turn;
            session.announce_desync = true;
        }
    }
}

void Arrived(Session& session, const std::vector<uint8_t>& message, Peer* from) {
    Wire::Reader in(message);
    const uint8_t kind = in.U8();
    if (kind == kWelcome && session.role == Role::Client) {
        session.seat = in.U8();
        Core::GameSetup setup;
        if (!Wire::Read(in, setup)) {
            session.failure = "a garbled welcome from the host";
            return;
        }
        session.setup = setup;
        const uint32_t restore = in.U32();
        if (!in.ok || in.at + restore > message.size()) {
            session.failure = "a garbled welcome from the host";
            return;
        }
        const auto from = message.begin() + static_cast<std::ptrdiff_t>(in.at);
        session.restore.assign(from, from + static_cast<std::ptrdiff_t>(restore));
        session.humans.clear();
        for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
            if (setup.players[seat].active && !setup.players[seat].computer) {
                session.humans.push_back(static_cast<Core::PlayerId>(seat));
            }
        }
        session.welcomed = true;
        return;
    }
    if (kind == kStart && session.role == Role::Client) {
        Begin(session);
        return;
    }
    if (kind == kDesync) {
        const uint32_t turn = in.U32();
        if (!session.desynced) {
            session.desynced = true;
            session.desync_turn = turn;
        }
        if (session.role == Role::Host) Broadcast(session, message, from != nullptr ? from->seat : 0);
        return;
    }
    if (kind != kTurn) {
        session.failure = "a message of an unknown kind";
        return;
    }
    const Core::PlayerId seat = in.U8();
    const uint32_t turn = in.U32();
    const uint32_t digest_turn = in.U32();
    const uint64_t digest = in.U64();
    std::vector<Core::Command> commands(in.U16());
    for (Core::Command& c : commands) {
        if (!Wire::Read(in, c)) break;
    }
    if (!in.ok) {
        session.failure = "a garbled turn";
        return;
    }
    // A client speaks only for its own seat.
    if (session.role == Role::Host && from != nullptr && seat != from->seat) {
        session.failure = "a client sent orders for another seat";
        return;
    }
    session.inbox[turn][seat] = std::move(commands);
    if (digest_turn != kNoDigest) {
        session.digests[digest_turn][seat] = digest;
        Compare(session, digest_turn);
    }
    if (session.role == Role::Host) Broadcast(session, message, seat);
}

} // namespace

bool Host(Session& session, uint16_t port, const Core::GameSetup& setup, int people) {
    // A restored game's save, given before hosting, is kept.
    std::vector<uint8_t> restore = std::move(session.restore);
    session = Session{};
    session.restore = std::move(restore);
    session.role = Role::Host;
    session.seat = 1;
    session.setup = setup;
    session.expected = people;
    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
        if (setup.players[seat].active && !setup.players[seat].computer) {
            session.humans.push_back(static_cast<Core::PlayerId>(seat));
        }
    }
    session.listener = HAL::Net::Listen(port);
    if (session.listener == HAL::Net::kNoSocket) {
        session.failure = "cannot listen on that port";
        return false;
    }
    if (people <= 1) Begin(session);
    return true;
}

bool Join(Session& session, const std::string& host, uint16_t port) {
    session = Session{};
    session.role = Role::Client;
    Peer peer;
    if (!HAL::Net::Connect(host, port, peer.connection)) {
        session.failure = "cannot reach the host";
        return false;
    }
    peer.seat = 1;
    session.peers.push_back(std::move(peer));
    return true;
}

void Pump(Session& session) {
    if (session.role == Role::Offline) return;
    // The host seats whoever connects, in seat order, until the table is full.
    if (session.role == Role::Host && !session.started) {
        HAL::Net::Connection incoming;
        while (HAL::Net::Accept(session.listener, incoming)) {
            Peer peer;
            peer.connection = std::move(incoming);
            peer.seat = session.humans.size() > session.peers.size() + 1
                ? session.humans[session.peers.size() + 1] : 0;
            if (peer.seat == 0) {
                HAL::Net::Close(peer.connection);
                continue;
            }
            HAL::Net::Send(peer.connection, Welcome(session, peer.seat));
            session.peers.push_back(std::move(peer));
        }
        if (static_cast<int>(session.peers.size()) + 1 >= session.expected) {
            Broadcast(session, Start(), 0);
            Begin(session);
        }
    }
    if (session.announce_desync) {
        session.announce_desync = false;
        Wire::Writer out;
        out.U8(kDesync);
        out.U32(session.desync_turn);
        Broadcast(session, out.bytes, 0);
    }
    std::vector<uint8_t> message;
    for (Peer& peer : session.peers) {
        HAL::Net::Pump(peer.connection);
        while (HAL::Net::Receive(peer.connection, message)) Arrived(session, message, &peer);
        if (peer.connection.closed && session.failure.empty()) {
            session.failure = "a player's connection was lost";
        }
    }
    for (Peer& peer : session.peers) HAL::Net::Pump(peer.connection);
}

void Issue(Session& session, const Core::Command& command) {
    session.outbox.push_back(command);
}

bool NextTick(Session& session, std::vector<Core::Command>& due) {
    due.clear();
    if (!session.started || session.desynced || !session.failure.empty()) return false;
    if (session.tick_in_turn > 0) return true;

    // The start of a turn: everyone's orders for it have to be in.
    auto& orders = session.inbox[session.turn];
    for (Core::PlayerId seat : session.humans) {
        if (orders.find(seat) == orders.end()) return false;
    }
    for (Core::PlayerId seat : session.humans) {
        for (const Core::Command& c : orders[seat]) due.push_back(c);
    }
    session.inbox.erase(session.turn);

    // And this machine's orders since the last turn go out, for the turn
    // they can still reach, with the digest of the turn just finished.
    const uint32_t target = session.turn + kDelayTurns;
    uint32_t digest_turn = kNoDigest;
    uint64_t digest = 0;
    if (session.turn > 0) {
        digest_turn = session.turn - 1;
        const auto found = session.digests.find(digest_turn);
        if (found != session.digests.end() && found->second.count(session.seat)) {
            digest = found->second.at(session.seat);
        }
    }
    const auto message = Turn(session.seat, target, digest_turn, digest, session.outbox);
    session.inbox[target][session.seat] = session.outbox;
    session.outbox.clear();
    if (session.role == Role::Host) {
        Broadcast(session, message, 0);
    } else {
        for (Peer& peer : session.peers) HAL::Net::Send(peer.connection, message);
    }
    for (Peer& peer : session.peers) HAL::Net::Pump(peer.connection);
    return true;
}

void TickDone(Session& session, const entt::registry& registry) {
    if (++session.tick_in_turn < kTicksPerTurn) return;
    session.tick_in_turn = 0;
    session.digests[session.turn][session.seat] = ECS::Digest::Of(registry);
    Compare(session, session.turn);
    // Old digests are of no more use once compared.
    if (session.turn > 64) session.digests.erase(session.digests.begin(),
                                                 session.digests.lower_bound(session.turn - 64));
    ++session.turn;
}

void Leave(Session& session) {
    for (Peer& peer : session.peers) HAL::Net::Close(peer.connection);
    if (session.listener != HAL::Net::kNoSocket) HAL::Net::CloseListener(session.listener);
    session = Session{};
}

} // namespace Net
