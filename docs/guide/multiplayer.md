# Multiplayer

Network games are played over TCP, on the local network or across the
internet, with up to eight players, people and computer players mixed. Every
machine runs the whole game and only the players' orders travel, so every
player needs the same version of OpenAoK. How that works is explained in
[Lockstep multiplayer](/engine/multiplayer).

## Hosting and joining

The main menu's **Multiplayer** creates a game or joins one. Its connection
list has TCP/IP and the local network:

- **On the local network**, games are found by themselves: the hosts on the
  same network answer and are listed.
- **Over TCP/IP**, Show Games asks for the host's address. The games you have
  joined by address are kept under Recent Games.

A host answers anyone looking for games on UDP port 27015. To be joined from
outside its network, the host's router has to let that port and the game's
port through.

## The lobby

The host's lobby has eight rows. Each is open to a person, closed, a computer
player or a person's, with a civilisation, a player number, a team, a ready
check and a ping. A person who joins takes the first open row and changes only
their own row; the host sets the game and the other rows. Chat in the lobby
reaches everyone.

The host starts once everyone is ready and there are two players or more.

## From the command line

Without the lobby, a host waits for a number of people and starts when they are
all in:

```sh
./OpenAoK --host 27016 --players 2      # on the host
./OpenAoK --join 192.168.1.10:27016     # on the other machine
```

Each client gets its seat and the game's setup from the host.

## If a game goes out of step

Every turn, each machine sends a digest of its world with its orders. If two
machines ever disagree, the game is out of sync: every machine stops with the
game's desync message. That should not happen between machines on the same
version; if it does, please
[report it](https://github.com/Zaydme/openaok/issues) with how the game was
set up.
