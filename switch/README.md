# Nintendo Switch

OpenAoK as Nintendo Switch homebrew: `OpenAoK.nro`, started from the Homebrew
Menu on a Switch that runs homebrew under custom firmware such as Atmosphère.

The `.nro` holds none of the game. The game's data is a folder you make on a
computer from your own CD and copy to the SD card beside it.

## What you need

- A Switch that runs homebrew.
- On a computer: your copy of the original CD in `original_game_1999/`, and
  the tools that prepare it (Python 3, CMake, a C++ compiler and `cabextract`;
  see the main README).
- To build the `.nro` yourself: devkitPro's Switch toolchain (`switch-dev`,
  `switch-sdl2` and `switch-freetype`), or Docker. A released `OpenAoK.nro` can
  be used instead.

## Build

```sh
switch/build.sh
```

writes `build-switch/OpenAoK.nro`. It uses devkitPro's toolchain when
`DEVKITPRO` is set, and otherwise devkitPro's Docker image
(`devkitpro/devkita64`). With the toolchain installed, the same is:

```sh
cmake -S . -B build-switch -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
cmake --build build-switch
```

## Make the SD card's folder

```sh
python3 switch/package.py --out /path/to/the/sd/card
```

prepares the game's data from `original_game_1999/` as `build.sh` does, and
writes:

```
switch/openaok/OpenAoK.nro        the game
switch/openaok/game/              its data, about 350 MB
```

Without `--out` the folder is written to `build-switch/sd/`, to copy to the
card's root yourself. Run it again after an update: only what changed is
copied. The folder holds your copy of the game, so keep it to your own console.

## Play

Hold R while starting any game: the Homebrew Menu opens with all of the
console's memory, which OpenAoK needs. Choose OpenAoK. Started from the Album
instead, it says so and returns to the menu.

The profile, saved games and scenarios are kept in `switch/openaok/` (`Profile`,
`SaveGame`, `Scenario`), beside the data rather than in it. What the engine
prints goes to `switch/openaok/openaok.log`.

## Controls

In handheld mode the touch screen plays as on a phone:

| Gesture | Does |
|---|---|
| Tap | Left click |
| Hold still for a moment | Right click (the controller buzzes) |
| Drag one finger | Left drag: box-select units, lay a wall |
| Drag two fingers | Scroll the map |

The controller plays as a mouse and the game's own hotkeys:

| Control | Does |
|---|---|
| Left stick | Moves the pointer, the faster the further it is pushed |
| Right stick | Scrolls the map |
| A | Left mouse button; held while the pointer moves, a drag |
| B | Right click |
| X | Next idle villager |
| Y | Escape: backs out of whatever is half done |
| L | Go to the Town Center |
| R | Next idle military unit |
| ZL, held | Shift: add to a selection, queue orders, train five |
| ZR | Go to the last notification |
| + | Game menu |
| − | Chat |
| D-pad up | Pause |
| D-pad down | Go to the selected unit |
| D-pad left, right | Slower, faster |

With the pointer moved by the stick, the screen's edges scroll the map as with a
mouse.

Chat and a saved game's name bring up the system keyboard; OK finishes the
line, and backing out of the keyboard cancels it. On the Options and
Multiplayer screens and in the scenario editor, − brings up the keyboard for
the field being typed into.

## Not yet

- Network games, which start from the command line.
