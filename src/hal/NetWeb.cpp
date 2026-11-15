// The network boundary in the browser, where a page can open WebSockets but
// cannot listen or use TCP. A host and the players who join it all connect out
// to the relay in web/server.js, which pairs them; the game's bytes cross it
// unchanged, so `Send` and `Receive` (hal/Net.cpp) are the ones every platform
// uses. A listener is the host's control connection to the relay, one room,
// and each player who joins is a connection of their own.
#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/websocket.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <unordered_map>

#include "hal/Net.h"

namespace HAL::Net {

namespace {

// The longest a call that has to wait for the relay waits.
constexpr int kWaitMs = 5000;

// What the callbacks have heard on one WebSocket, until `Pump` or `Accept`
// takes it.
struct Channel {
    bool open = false;
    bool closed = false;            // closed, refused or broken
    std::vector<uint8_t> received;  // a player's bytes, not yet in a Connection
    // A listener's room, and the players the relay says have joined.
    bool ready = false;
    uint16_t room = 0;
    std::deque<unsigned> joined;
    // A datagram socket: what has arrived, each with the port it came from.
    bool datagram = false;
    std::deque<std::vector<uint8_t>> datagrams;
};

std::unordered_map<int, Channel> g_channels;

EM_JS_DEPS(net_web_deps, "$stringToNewUTF8");

// The relay's address, a malloc'd string: the page's own server unless the
// page set Module.relayUrl.
EM_JS(char*, RelayUrl, (), {
    var url;
    if (typeof Module !== "undefined" && Module["relayUrl"]) {
        url = Module["relayUrl"];
    } else if (typeof location !== "undefined") {
        url = (location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/relay";
    } else {
        url = "ws://127.0.0.1:8080/relay";
    }
    return stringToNewUTF8(url);
});

// The host name the page was served from, a malloc'd string.
EM_JS(char*, PageHost, (), {
    return stringToNewUTF8(typeof location !== "undefined" ? location.hostname : "");
});

EM_BOOL OnOpen(int, const EmscriptenWebSocketOpenEvent* event, void*) {
    const auto it = g_channels.find(event->socket);
    if (it != g_channels.end()) it->second.open = true;
    return EM_TRUE;
}

EM_BOOL OnClose(int, const EmscriptenWebSocketCloseEvent* event, void*) {
    const auto it = g_channels.find(event->socket);
    if (it != g_channels.end()) {
        it->second.open = false;
        it->second.closed = true;
    }
    return EM_TRUE;
}

EM_BOOL OnError(int, const EmscriptenWebSocketErrorEvent* event, void*) {
    const auto it = g_channels.find(event->socket);
    if (it != g_channels.end()) it->second.closed = true;
    return EM_TRUE;
}

EM_BOOL OnMessage(int, const EmscriptenWebSocketMessageEvent* event, void*) {
    const auto it = g_channels.find(event->socket);
    if (it == g_channels.end()) return EM_TRUE;
    Channel& channel = it->second;
    if (!event->isText) {
        if (channel.datagram) {
            // Two bytes of source port, then the datagram: kept whole.
            if (event->numBytes > 2) channel.datagrams.emplace_back(event->data + 2, event->data + event->numBytes);
        } else {
            channel.received.insert(channel.received.end(), event->data, event->data + event->numBytes);
        }
        return EM_TRUE;
    }
    // The relay's words to a host: "room N" once the room is open, "peer ID"
    // for each player who joins.
    const std::string text(reinterpret_cast<const char*>(event->data));
    unsigned number = 0;
    if (std::sscanf(text.c_str(), "room %u", &number) == 1) {
        channel.room = static_cast<uint16_t>(number);
        channel.ready = true;
    } else if (std::sscanf(text.c_str(), "peer %u", &number) == 1) {
        channel.joined.push_back(number);
    }
    return EM_TRUE;
}

// A WebSocket to the relay with `query` (host=PORT, join=PORT or peer=ID), or
// -1.
int Open(const std::string& query) {
    char* base = RelayUrl();
    const std::string url = std::string(base) + "?" + query;
    std::free(base);
    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = url.c_str();
    const int ws = emscripten_websocket_new(&attributes);
    if (ws <= 0) return -1;
    g_channels[ws] = Channel{};
    emscripten_websocket_set_onopen_callback(ws, nullptr, OnOpen);
    emscripten_websocket_set_onclose_callback(ws, nullptr, OnClose);
    emscripten_websocket_set_onerror_callback(ws, nullptr, OnError);
    emscripten_websocket_set_onmessage_callback(ws, nullptr, OnMessage);
    return ws;
}

void Drop(int ws) {
    emscripten_websocket_close(ws, 1000, "bye");
    emscripten_websocket_delete(ws);
    g_channels.erase(ws);
}

// Lets the page run until `done` holds or kWaitMs has passed.
template <class Done>
bool Await(Done done) {
    for (int waited = 0; waited < kWaitMs; waited += 10) {
        if (done()) return true;
        emscripten_sleep(10);
    }
    return done();
}

} // namespace

Socket Listen(uint16_t port) {
    const int ws = Open("host=" + std::to_string(port));
    if (ws < 0) return kNoSocket;
    const bool opened = Await([&] {
        const Channel& channel = g_channels[ws];
        return channel.ready || channel.closed;
    });
    if (!opened || !g_channels[ws].ready) {
        Drop(ws);
        return kNoSocket;
    }
    return ws;
}

uint16_t BoundPort(Socket listener) {
    const auto it = g_channels.find(static_cast<int>(listener));
    return it == g_channels.end() ? 0 : it->second.room;
}

bool Accept(Socket listener, Connection& out) {
    const auto it = g_channels.find(static_cast<int>(listener));
    if (it == g_channels.end() || it->second.joined.empty()) return false;
    const unsigned id = it->second.joined.front();
    it->second.joined.pop_front();
    const int ws = Open("peer=" + std::to_string(id));
    if (ws < 0) return false;
    out = Connection{};
    out.socket = ws;
    return true;
}

// The relay is the page's own server, so the host name is not used: the port
// names the room.
bool Connect(const std::string&, uint16_t port, Connection& out) {
    const int ws = Open("join=" + std::to_string(port));
    if (ws < 0) return false;
    const bool settled = Await([&] {
        const Channel& channel = g_channels[ws];
        return channel.open || channel.closed;
    });
    if (!settled || !g_channels[ws].open) {
        Drop(ws);
        return false;
    }
    out = Connection{};
    out.socket = ws;
    return true;
}

bool Dial(const std::string&, uint16_t port, Connection& out) {
    out = Connection{};
    const int ws = Open("join=" + std::to_string(port));
    if (ws < 0) return false;
    out.socket = ws;
    return true;
}

int Dialled(Connection& connection) {
    if (connection.socket == kNoSocket || connection.closed) return -1;
    const auto it = g_channels.find(static_cast<int>(connection.socket));
    if (it == g_channels.end()) return -1;
    if (it->second.open) return 1;
    if (it->second.closed) {
        Close(connection);
        return -1;
    }
    return 0;
}

void Close(Connection& connection) {
    if (connection.socket != kNoSocket) Drop(static_cast<int>(connection.socket));
    connection.socket = kNoSocket;
    connection.closed = true;
}

void CloseListener(Socket listener) {
    if (listener != kNoSocket) Drop(static_cast<int>(listener));
}

void Pump(Connection& connection) {
    if (connection.socket == kNoSocket) return;
    const int ws = static_cast<int>(connection.socket);
    const auto it = g_channels.find(ws);
    if (it == g_channels.end()) {
        Close(connection);
        return;
    }
    Channel& channel = it->second;
    if (!channel.received.empty()) {
        connection.incoming.insert(connection.incoming.end(), channel.received.begin(), channel.received.end());
        channel.received.clear();
    }
    if (channel.open && !connection.outgoing.empty()) {
        emscripten_websocket_send_binary(ws, connection.outgoing.data(), static_cast<uint32_t>(connection.outgoing.size()));
        connection.outgoing.clear();
    }
    if (channel.closed) Close(connection);
}

// Datagrams go through the relay, which delivers each to the socket holding the
// port it names, the way a network would to a machine: a browser cannot send
// UDP. Messages for everyone on the network (an empty host) go to the port too.
Socket OpenDatagram(uint16_t port) {
    const int ws = Open("dgram=" + std::to_string(port));
    if (ws < 0) return kNoSocket;
    g_channels[ws].datagram = true;
    const bool opened = Await([&] {
        const Channel& channel = g_channels[ws];
        return channel.ready || channel.closed;
    });
    if (!opened || !g_channels[ws].ready) {
        Drop(ws);
        return kNoSocket;
    }
    return ws;
}

void CloseDatagram(Socket s) {
    if (s != kNoSocket) Drop(static_cast<int>(s));
}

bool SendDatagram(Socket s, const std::string&, uint16_t port, const std::vector<uint8_t>& message) {
    if (s == kNoSocket) return false;
    const int ws = static_cast<int>(s);
    const auto it = g_channels.find(ws);
    if (it == g_channels.end() || !it->second.open) return false;
    std::vector<uint8_t> out;
    out.reserve(message.size() + 2);
    out.push_back(static_cast<uint8_t>(port));
    out.push_back(static_cast<uint8_t>(port >> 8));
    out.insert(out.end(), message.begin(), message.end());
    return emscripten_websocket_send_binary(ws, out.data(), static_cast<uint32_t>(out.size())) >= 0;
}

bool ReceiveDatagram(Socket s, std::vector<uint8_t>& message, std::string& from) {
    const auto it = g_channels.find(static_cast<int>(s));
    if (it == g_channels.end() || it->second.datagrams.empty()) return false;
    message = std::move(it->second.datagrams.front());
    it->second.datagrams.pop_front();
    char* host = PageHost();
    from = host;
    std::free(host);
    return true;
}

std::string LocalAddress() {
    char* host = PageHost();
    const std::string address = host;
    std::free(host);
    return address;
}

} // namespace HAL::Net

#endif // __EMSCRIPTEN__
