# macOS and Linux

On macOS and Linux OpenAoK is built from its source code, which takes a few
minutes. The build prepares the game's data from your CD the first time.

## Install the tools

::: code-group

```sh [macOS]
# with the Xcode command-line tools
brew install cmake sdl2 freetype cabextract
```

```sh [Debian / Ubuntu]
sudo apt install build-essential cmake python3 libsdl2-dev libfreetype-dev zlib1g-dev cabextract
```

:::

## Build and play

Clone the repository, copy your CD into its `original_game_1999/` (see
[Your game files](/guide/game-files)), then:

```sh
./build.sh          # prepares the game data on the first run, then builds
./build.sh --run    # build and play
```

The first build takes about a minute longer while it unpacks and converts your
game files. The game runs from `build/`, where its data paths are relative to:

```sh
cd build && ./OpenAoK
```

The [command-line options](/guide/command-line) start straight into a game, a
scenario or a network game.
