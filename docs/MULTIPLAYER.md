# Multiplayer

A network game is lockstep: every machine runs the whole simulation, and only
the players' orders travel.

## How it works

The simulation is deterministic -- fixed point, a fixed tick, seeded randomness,
defined iteration order -- so two machines that start from the same setup and
carry out the same orders on the same ticks hold the same world. Computer
players run inside the tick on every machine, so they need no network at all.

## Turns

Time is cut into turns of 8 ticks (`Net::kTicksPerTurn`). An order given during
turn T is sent at once and carried out at turn T + 2 (`Net::kDelayTurns`) by every
machine, all players' orders for a turn in seat order. A tick may only run
once every player's orders for its turn have arrived, so a slow or silent
machine holds the others back rather than letting them play on without it.

## Desyncs

Each turn's orders carry the sender's digest of the world at the end of the
turn before (`ecs/Digest`: every entity's identity, place, hit points and
owner). The first turn two machines' digests differ is a desync. The machine
that finds it tells the others -- it stops sending turns, so they would never
see the mismatch otherwise -- and every machine stops with the game's desync message.

## The host and the lobby

The main menu's Multiplayer menu creates a game or joins one. Its connection
list has TCP/IP, under the name the CD's DirectX gives it, and the local
network (LANGUAGE.DLL 9620). On the local network the games are found by
themselves; over TCP/IP, Show Games asks for the host's address, with the
games last joined by address under Recent Games.

A host answers anyone looking for games: a datagram query on port 27015
(`Net::kPort`), broadcast on the local network or sent to one machine, gets
back the host's name, its game's port, how many people are seated and how
many rows are open, and whether it is a saved game hosted again.

The lobby is the host's (`net/Lobby`). It keeps the table: eight rows, each
open to a person, closed, a computer player or a person's, with a
civilisation, a player number, a team, I'm Ready! and a ping. A person who
joins takes the first open row. The host sets the game and what the other rows
are; each person changes only their own row, by asking the host, and the host
sends the whole table to everyone after every change. Chat goes the same way,
so every machine has its lines in the host's order. The host starts once
everyone is ready and there are two players or more: the rows in use become
the setup's seats in order, and each person's seat goes to them with the setup
in the welcome message.

From the command line (`--host PORT --players N`, `--join HOST:PORT`) there is
no lobby: the host seats each client as it connects, in seat order, sends it
its seat and the game's setup, and starts everyone when the table is full.

During play the host relays: clients send their orders to it and it passes
them on. Each machine knows its own seat (`Core::LocalPlayer`), which only the
interface ever asks.

## The wire

`net/Wire` writes every command and the setup as explicit little-endian bytes:
fixed widths, fixed-point values as their raw 64 bits, entities as their ids --
which every machine shares, because every machine builds the same world from the
same setup. `hal/Net` frames messages with a four-byte length over
non-blocking TCP sockets.

| Message | Contents |
|---|---|
| `H` hello | the lobby's version, the person's name |
| `L` table | the client's row, whether it is a restored game, the settings, the eight rows |
| `R` row | a client's civilisation, player number, team and I'm Ready! |
| `C` chat | a line, to the host; its speaker and the line, from the host |
| `P` ping, `p` its answer | the host's clock |
| `X` canceled | |
| `E` ejected | the LANGUAGE.DLL message why |
| `W` welcome | the client's seat, the setup |
| `S` start | |
| `T` turn | seat, turn, digest turn, digest, orders |
| `D` desync | the turn it was found |

## Checking it

The `lobby=` scenario plays a lobby over a loopback socket: the game found by
asking the host, a client seated in the first open row and shown the host's
settings, rows, ready checks and chat reaching both machines, a computer
player put in a row, a start refused until everyone is ready, a client that
leaves giving its row back, and the game started from the table in step.

The `net=` scenario plays a host and a client over a real loopback socket: the
wire round-trips every command, both players' orders happen on both machines,
an order for a unit the sender does not own is refused on both, the worlds stay
identical, a silent client holds the host back, and a world changed on one
machine is caught by both.

## Restoring a saved game

The Multiplayer menu's Restore hosts a saved game again: the host reads the
save's own lobby (`Save::ReadSetup`), whose seats become the table's rows and
stay as they were; each person who joins takes the row in their name, or the
first still free, and the host hands each client the whole save with its seat
in the welcome message (`Session::restore`); every machine then starts from
`Save::Read` instead of building a new world, and plays on in lockstep from
there. `net=` checks the hand-over, the restored world and play in step.
