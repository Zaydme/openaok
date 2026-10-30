#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The network boundary: TCP connections that carry whole messages.
//
// Everything above this -- the lockstep game, the lobby -- deals in messages,
// a length-prefixed run of bytes; this turns those into socket traffic and back
// without ever blocking, so a slow peer costs a frame nothing. The platform's
// socket calls stay in here.
namespace HAL::Net {

using Socket = intptr_t;
constexpr Socket kNoSocket = -1;

// One end of a connection, with what is waiting to go out and what has come
// in but is not yet a whole message.
struct Connection {
    Socket socket = kNoSocket;
    std::vector<uint8_t> outgoing;
    std::vector<uint8_t> incoming;
    bool closed = false;
};

// A socket listening for connections on `port` (0 asks the system for one;
// `BoundPort` says which). kNoSocket on failure.
Socket Listen(uint16_t port);
uint16_t BoundPort(Socket listener);

// A connection waiting on `listener`, or none yet.
bool Accept(Socket listener, Connection& out);

// Connects to `host`:`port`, waiting until it is established or refused.
bool Connect(const std::string& host, uint16_t port, Connection& out);

// Starts connecting to `host`:`port` without waiting; `Dialled` then says
// how it went: 1 established, 0 still under way, -1 refused or unreachable.
bool Dial(const std::string& host, uint16_t port, Connection& out);
int Dialled(Connection& connection);

void Close(Connection& connection);
void CloseListener(Socket listener);

// Queues a message.
void Send(Connection& connection, const std::vector<uint8_t>& message);

// Moves bytes both ways as far as the socket will without waiting.
void Pump(Connection& connection);

// The next whole message received, if there is one.
bool Receive(Connection& connection, std::vector<uint8_t>& message);

// Datagrams: single messages to anyone, for finding games. A datagram socket
// bound to `port` (0 for any) that may broadcast; kNoSocket on failure.
Socket OpenDatagram(uint16_t port);
void CloseDatagram(Socket s);
// Sends to `host`:`port`, or to every machine on the local network when
// `host` is empty.
bool SendDatagram(Socket s, const std::string& host, uint16_t port, const std::vector<uint8_t>& message);
// The next datagram waiting, and the address it came from.
bool ReceiveDatagram(Socket s, std::vector<uint8_t>& message, std::string& from);

// This machine's address on its network, as others would reach it; empty
// when it has none.
std::string LocalAddress();

} // namespace HAL::Net
