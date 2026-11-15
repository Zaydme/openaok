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

Serve one folder holding `index.html`, `data.json`, `OpenAoK.js`, `OpenAoK.wasm`,
`assets/` and `original_game_1999/SYSTEM/FONTS/` (links to them do), for example
with `python3 -m http.server 8080 --bind 127.0.0.1`, and open it. Extra flags
go in the address: `index.html?args=--fps,--perf`.

## What is not done

- Multiplayer: the network layer is TCP sockets, which a page cannot open.
- The data is served by the same host; letting the player choose their own
  folder or files is not built.
- The loops that wait on the screen run through ASYNCIFY, not the browser's
  frame callback.
