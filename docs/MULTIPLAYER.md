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

One machine hosts (`--host PORT --players N`). It seats clients in order as
they connect (`--join HOST:PORT`), sends each its seat and the game's setup,
and starts everyone when the table is full. During play it relays: clients
send their orders to it and it passes them on. Each machine knows its own seat
(`Core::LocalPlayer`), which only the interface ever asks.

## The wire

`net/Wire` writes every command and the setup as explicit little-endian bytes:
fixed widths, fixed-point values as their raw 64 bits, entities as their ids --
which every machine shares, because every machine builds the same world from the
same setup. `hal/Net` frames messages with a four-byte length over
non-blocking TCP sockets.

| Message | Contents |
|---|---|
| `W` welcome | the client's seat, the setup |
| `S` start | |
| `T` turn | seat, turn, digest turn, digest, orders |
| `D` desync | the turn it was found |

## Checking it

The `net=` scenario plays a host and a client over a real loopback socket: the
wire round-trips every command, both players' orders happen on both machines,
an order for a unit the sender does not own is refused on both, the worlds stay
identical, a silent client holds the host back, and a world changed on one
machine is caught by both.

## Restoring a saved game

The multiplayer screen's Create (Restore) hosts a saved game again: the
host reads the save's own lobby (`Save::ReadSetup`), seats its people as
they join, and hands each client the whole save with its seat in the
welcome message (`Session::restore`); every machine then starts from
`Save::Read` instead of building a new world, and plays on in lockstep from
there. `net=` checks the hand-over, the restored world and play in step.
