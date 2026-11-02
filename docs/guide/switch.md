# Nintendo Switch

OpenAoK runs as homebrew, from the Homebrew Menu, on a Switch that runs custom
firmware. The game is one file, `OpenAoK.nro`, which holds none of the game's
data; the data is a folder made on a computer from your own CD and copied to
the SD card beside it. Nothing needs building for the Switch itself.

## 1. Put the game on the card

Download `OpenAoK-switch-<version>.zip` from the
[releases](https://github.com/Zaydme/openaok/releases) and unpack it onto the
SD card's root. It holds `switch/openaok/OpenAoK.nro` and the licenses.

## 2. Make the game's data

Download the same release's source code (or clone the repository) and copy your
CD into its `original_game_1999/`. Preparing the data needs Python 3,
cabextract, CMake and a C++ compiler, as for [Android](/guide/android#_2-make-the-game-s-data).
Then, with the card in the computer:

```sh
python3 switch/package.py --out "/Volumes/SWITCH SD"
```

It prepares the game's data as `build.sh` does and writes
`switch/openaok/game/` on the card, beside the `.nro`: about 390 MB on a FAT32
card. Run it again after an update; only what changed is copied. Without
`--out` it writes to `build-switch/sd/`, to copy to the card yourself.

::: warning Card readers
Some USB card readers damage what they write to large cards. Hekate's USB mass
storage (Tools, USB Tools, SD Card) writes through the Switch's own card slot
instead.
:::

## 3. Play

Hold R while starting any game: the Homebrew Menu opens with all of the
console's memory, which OpenAoK needs. Choose OpenAoK. Started from the Album
instead, it says so and returns to the menu.

On its first start after a copy, OpenAoK checks every data file against
`game/manifest.txt`: a few seconds, with the progress on the screen. A file that
is missing or damaged on the card is named, and copying the folder again mends
it.

The profile, saved games and scenarios are kept in `switch/openaok/` beside the
data, and what the engine prints goes to `switch/openaok/openaok.log`.

In handheld mode the touch screen plays as on a phone; the controller plays as
a mouse and the game's hotkeys: see [Controls](/guide/controls#nintendo-switch).
