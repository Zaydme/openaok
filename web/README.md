# Web

OpenAoK in the browser, built with Emscripten (WebAssembly and WebGL). It plays
from your own copy of the game: the page fetches the prepared data from the
server it came from and writes it into the browser's file system. Nothing of the
game is in the build or the repository.

## Build

With the emsdk active, from the repository root, after the data is prepared
(`./build.sh` does it):

```sh
mkdir build-web && cd build-web
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
emmake make -j2
```

Each compile of the larger files and the link take a few GB of memory; keep `-j`
low on a machine with 8 GB.

## Run

```sh
python3 web/make_manifest.py   # writes web/data.json: the files the page fetches
```

```sh
node web/server.js [--port 8080] [--host 127.0.0.1] [--build build-web]
```

It serves the page, the build and the data the game reads, and is also the relay
for network games, so one process does both. Open `http://127.0.0.1:8080/`.
Extra flags go in the address: `index.html?args=--fps,--perf`.

It listens on this machine only. `--host 0.0.0.0` lets others on the network
play, and then it serves your game data to everyone who can reach it: share it
only with people who own the game.

## Multiplayer

A page cannot listen or use TCP, so the host and the players all connect out to
the relay in `server.js`, which pairs them (`src/hal/NetWeb.cpp`; the protocol is
described at the top of `server.js`). Open the page in each browser, host with
Create and join from the list, as on a network: the relay stands in for the
network, and games are found through it. One game at a time can be hosted on a
relay, because the host always takes the room the game's port names.

## What is not done

- A relay open to the internet: it has no authentication, no limit per address
  and only one game at a time; it is meant for a machine or network you control.
- The data is served by the same host; letting the player choose their own
  folder or files is not built.
- The loops that wait on the screen run through ASYNCIFY, not the browser's
  frame callback.
