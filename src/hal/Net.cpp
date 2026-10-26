#include "hal/Net.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace HAL::Net {

namespace {

// Messages are framed by a four-byte little-endian length.
constexpr size_t kHeader = 4;
// The largest message accepted (a restored game's welcome carries the whole
// saved game); anything claiming more is garbage.
constexpr uint32_t kMaxMessage = 64u << 20;

void Startup() {
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        started = true;
    }
#endif
}

void NonBlocking(Socket s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on);
#else
    const int flags = fcntl(static_cast<int>(s), F_GETFL, 0);
    fcntl(static_cast<int>(s), F_SETFL, flags | O_NONBLOCK);
#endif
}

void NoDelay(Socket s) {
    int on = 1;
    setsockopt(static_cast<int>(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on),
               sizeof(on));
}

bool WouldBlock() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void CloseSocket(Socket s) {
    if (s == kNoSocket) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(s));
#else
    close(static_cast<int>(s));
#endif
}

} // namespace

Socket Listen(uint16_t port) {
    Startup();
    const Socket s = static_cast<Socket>(socket(AF_INET, SOCK_STREAM, 0));
    if (s < 0) return kNoSocket;
    int reuse = 1;
    setsockopt(static_cast<int>(s), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(static_cast<int>(s), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(static_cast<int>(s), 8) != 0) {
        CloseSocket(s);
        return kNoSocket;
    }
    NonBlocking(s);
    return s;
}

uint16_t BoundPort(Socket listener) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (getsockname(static_cast<int>(listener), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

bool Accept(Socket listener, Connection& out) {
    const Socket s = static_cast<Socket>(accept(static_cast<int>(listener), nullptr, nullptr));
    if (s < 0) return false;
    NonBlocking(s);
    NoDelay(s);
    out = Connection{};
    out.socket = s;
    return true;
}

bool Connect(const std::string& host, uint16_t port, Connection& out) {
    Startup();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        return false;
    }
    const Socket s = static_cast<Socket>(socket(found->ai_family, found->ai_socktype, found->ai_protocol));
    const bool ok = s >= 0 &&
                    connect(static_cast<int>(s), found->ai_addr, static_cast<socklen_t>(found->ai_addrlen)) == 0;
    freeaddrinfo(found);
    if (!ok) {
        CloseSocket(s);
        return false;
    }
    NonBlocking(s);
    NoDelay(s);
    out = Connection{};
    out.socket = s;
    return true;
}

void Close(Connection& connection) {
    CloseSocket(connection.socket);
    connection.socket = kNoSocket;
    connection.closed = true;
}

void CloseListener(Socket listener) { CloseSocket(listener); }

void Send(Connection& connection, const std::vector<uint8_t>& message) {
    const uint32_t length = static_cast<uint32_t>(message.size());
    for (size_t b = 0; b < kHeader; ++b) {
        connection.outgoing.push_back(static_cast<uint8_t>(length >> (8 * b)));
    }
    connection.outgoing.insert(connection.outgoing.end(), message.begin(), message.end());
}

void Pump(Connection& connection) {
    if (connection.socket == kNoSocket) return;
    while (!connection.outgoing.empty()) {
        const auto sent = send(static_cast<int>(connection.socket),
                               reinterpret_cast<const char*>(connection.outgoing.data()),
                               static_cast<int>(connection.outgoing.size()), 0);
        if (sent > 0) {
            connection.outgoing.erase(connection.outgoing.begin(), connection.outgoing.begin() + sent);
            continue;
        }
        if (sent < 0 && WouldBlock()) break;
        Close(connection);
        return;
    }
    uint8_t buffer[4096];
    for (;;) {
        const auto got = recv(static_cast<int>(connection.socket), reinterpret_cast<char*>(buffer),
                              sizeof(buffer), 0);
        if (got > 0) {
            connection.incoming.insert(connection.incoming.end(), buffer, buffer + got);
            continue;
        }
        if (got < 0 && WouldBlock()) break;
        Close(connection);   // closed by the peer, or broken
        return;
    }
}

bool Receive(Connection& connection, std::vector<uint8_t>& message) {
    if (connection.incoming.size() < kHeader) return false;
    uint32_t length = 0;
    for (size_t b = 0; b < kHeader; ++b) length |= uint32_t(connection.incoming[b]) << (8 * b);
    if (length > kMaxMessage) {
        Close(connection);
        return false;
    }
    if (connection.incoming.size() < kHeader + length) return false;
    message.assign(connection.incoming.begin() + kHeader,
                   connection.incoming.begin() + static_cast<long>(kHeader + length));
    connection.incoming.erase(connection.incoming.begin(),
                              connection.incoming.begin() + static_cast<long>(kHeader + length));
    return true;
}

} // namespace HAL::Net
