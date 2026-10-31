<p align="center">
  <img src="docs/images/logo.png" width="680" alt="OpenAoK">
</p>

<p align="center">
  <b>A free, open-source engine for Age of Empires II: The Age of Kings, on your desktop and in your pocket.</b>
</p>

<p align="center">
  <img src="docs/images/phone-town.png" width="820" alt="OpenAoK running on an Android phone">
</p>

## Why

Playing Age of Empires II on a phone has been a dream of mine since I was a
kid. Microsoft was never going to make that happen, so I did it myself.

OpenAoK is a new engine, written from scratch, that plays the original 1999
game from your own copy of its CD: the same units, maps, art, sounds, music,
movies and campaigns, on macOS, Linux, Android phones and the Nintendo Switch.

## What it is

- **Written from scratch.** OpenAoK contains no code or data from the original
  game. It is modern C++20 on SDL 2, data-oriented (EnTT), with a deterministic
  fixed-point simulation, and it reads the original's data files from your own
  copy and implements their rules.
- **The original, faithfully.** Every unit's numbers, every technology,
  civilisation and map script come from the 1999 data. The interface is drawn
  from the original art, laid out as the game laid it out, in its own fonts and
  words.
- **Your copy, your data.** The repository contains no game files. You copy
  your CD into a folder, and the build prepares everything it needs from it.

## Screenshots

<table>
  <tr>
    <td><img src="docs/images/economy.jpg" alt="A computer player's town after fifteen minutes"></td>
    <td><img src="docs/images/battle.jpg" alt="Two armies meeting"></td>
  </tr>
  <tr>
    <td><img src="docs/images/town.jpg" alt="The bonus scenario"></td>
    <td><img src="docs/images/tech-tree.jpg" alt="The technology tree"></td>
  </tr>
  <tr>
    <td><img src="docs/images/campaign.jpg" alt="The William Wallace campaign map"></td>
    <td><img src="docs/images/achievements.jpg" alt="The Achievements timeline"></td>
  </tr>
</table>

### On a phone

<p align="center">
  <img src="docs/images/phone-build.png" width="49%" alt="Building on a phone, with the larger interface">
  <img src="docs/images/phone-start.png" width="49%" alt="A new random map game on a phone">
</p>

On a touch screen the game is played as with a mouse, and the controls are
surprisingly comfortable: tap to click, hold to right-click, drag one finger to
select, two fingers to scroll the map, pinch to zoom. The top
bar, the panel and the text can each be drawn larger (Options → Top Bar Size,
Panel Size, Text Size), and in widescreen the interface reaches the screen's
edges.

<p align="center">
  <img src="docs/images/switch-controls.svg" width="900" alt="The Switch's controls: the left stick moves the pointer, the right stick scrolls the map and with ZL held zooms, A and B are the left and right mouse buttons, plus is the game menu, minus is chat, and the other buttons are the game's hotkeys">
</p>

## What works

- **Random maps** from the original map scripts: lands, terrain, elevation,
  cliffs, shores, objects, at every map size.
- **The rules**: every unit's numbers, all 426 technologies and their effects,
  each civilisation's tech tree and bonuses, the four ages.
- **Economy**: gathering, drop sites, construction, farms, housing, production
  and research queues, the Market, trade carts and cogs, tribute.
- **War**: attack and armour classes, missiles, splash damage, formations and
  stances, garrisons and the town bell, monks and relics, siege, walls and gates.
- **Water**: docks, fishing, the warship lines, transports landing on any shore.
- **Computer players** running the original AI scripts in an interpreter of the
  script language, at every difficulty.
- **Campaigns and scenarios**, with their triggers, narration and pictures; the
  scenario editor; saved and recorded games.
- **Victory** by conquest, Wonder or relics; Regicide and Death Match; diplomacy,
  allied victory, chat, taunts, flares and the cheat codes.
- **Presentation**: sprites straight from the original SLPs with player colours
  and shadows, terrain blending, fog of war, the original interface, sounds,
  music and the start-up movies.
- **Multiplayer** in lockstep over TCP, with desync detection: games on the
  local network found by themselves, others joined by address, and the lobby
  with its chat and ready checks.

What's left, and the known issues, are in [ROADMAP.md](ROADMAP.md).

## How it works

The simulation runs on 32.32 fixed point at a fixed 60 Hz, so the same orders
produce the same world on every machine. Lockstep multiplayer and the
self-check rely on this. Rendering, audio and the interface only read the
simulation; orders reach it as commands. Units move at continuous positions;
the tile grid holds buildings, terrain and path search.

At run time the engine reads the original DRS archives directly: SLP sprites,
terrain, interface art, sounds, map scripts and AI scripts. The rule tables in
the dat and LANGUAGE.DLL are converted to JSON once, at build time, by the tools
in `tools/data`. The movies are decoded by the engine's own Indeo 5 decoder.

More in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Getting the game files

OpenAoK needs **your own, legally obtained copy** of *Age of Empires II: The
Age of Kings* (1999, version 1.0, the original CD). It does not work with The
Conquerors expansion or the HD and Definitive editions, which use different
data.

Copy the CD's files into a folder named `original_game_1999` at the top of the
repository. From a disc or a mounted image on macOS, for example:

```sh
cp -R /Volumes/AOE2/. original_game_1999/
```

The folder should then hold `GAME/`, `SYSTEM/` and `GOODIES/`. That's all: the
build unpacks and converts what it needs. Everything taken from your copy stays
on your machine and is ignored by git. Please don't share it, or the builds you
make from it: a desktop build contains your game data. The Android APK and the
Switch's `OpenAoK.nro` hold none of it; the data folders made for them do.

The default hotkeys are read from the CD's `GAME/EMPIRES2.EXE`. On a CD
protected by SafeDisc that file's code is encrypted, so they can't be: the
build says so and goes on, and the game plays without hotkeys.

## Building for desktop (macOS, Linux)

Install the tools:

```sh
# macOS (with the Xcode command-line tools)
brew install cmake sdl2 freetype cabextract

# Debian / Ubuntu
sudo apt install build-essential cmake python3 libsdl2-dev libfreetype-dev zlib1g-dev cabextract
```

Then build and run:

```sh
./build.sh          # prepares the game data on the first run, then builds
./build.sh --run    # build and play
```

The first build takes about a minute longer while it unpacks and converts your
game files. Run the game from `build/`:

```sh
cd build && ./OpenAoK
```

## On Android

OpenAoK runs on arm64 phones and tablets with Android 10 or later. The APK
holds none of the game's data; the data is a folder made on a computer from
your own CD and copied to the phone.

1. Install `OpenAoK-<version>.apk` from the
   [releases](https://github.com/Zaydme/openaok/releases).
2. With the same release's source code and your CD in `original_game_1999/`,
   run `python3 android/package.py`. It needs the same tools as the Switch's
   data (below) and writes `android/build/OpenAoK-data/`, about 350 MB.
3. Copy `OpenAoK-data` to the phone, into Download for example, open OpenAoK
   and choose the folder. The app copies it in; then the folder can go.

More, building the APK yourself included, in
[android/README.md](android/README.md).

## On the Nintendo Switch

OpenAoK runs as homebrew, from the Homebrew Menu, on a Switch that runs custom
firmware. The game is one file, `OpenAoK.nro`, which holds none of the game's
data; the data is a folder made on a computer from your own CD and copied to
the SD card beside it. Nothing needs building for the Switch itself.

**1. Put the game on the card.** Download `OpenAoK-switch-<version>.zip` from
the [releases](https://github.com/Zaydme/openaok/releases) and unpack it onto
the SD card's root. It holds `switch/openaok/OpenAoK.nro` and the licenses.

**2. Make the game's data.** Download the same release's source code (or clone
the repository) and copy your CD into its `original_game_1999/`, as above.
Preparing the data needs Python 3, cabextract, CMake and a C++ compiler, which
build only a small helper that reads the game's rules:

```sh
# macOS (with the Xcode command-line tools)
brew install cmake cabextract

# Debian / Ubuntu
sudo apt install build-essential cmake python3 zlib1g-dev cabextract
```

Then, with the card in the computer:

```sh
python3 switch/package.py --out "/Volumes/SWITCH SD"
```

It prepares the game's data as `build.sh` does and writes `switch/openaok/game/`
on the card, beside the `.nro`: about 390 MB. Run it again after an update;
only what changed is copied. Without `--out` it writes to `build-switch/sd/`,
to copy to the card yourself. Some USB card readers damage what they write to
large cards; Hekate's USB mass storage (Tools, USB Tools, SD Card) writes
through the Switch's own card slot instead.

**3. Play.** Put the card back, hold R while starting any game, which opens the
Homebrew Menu with all of the console's memory, and choose OpenAoK.

In handheld mode the touch screen plays as on a phone; the controller plays as a
mouse and the game's hotkeys, as above. Chat and a saved game's name bring up
the system keyboard. More in [switch/README.md](switch/README.md).

### Building the `.nro` yourself

With Docker, or with devkitPro's Switch toolchain (`switch-dev`, `switch-sdl2`,
`switch-freetype`) when `DEVKITPRO` is set:

```sh
switch/build.sh               # writes build-switch/OpenAoK.nro
```

On a Mac, Docker can be `brew install colima docker && colima start`. Then
`python3 switch/package.py` copies the `.nro` you built to the card with the
data.

## Playing

Start from the main menu, or straight into a game from the command line:

| Option | |
|---|---|
| `--game` | Straight into a random map game against the computer |
| `--map NAME` | Arabia, Archipelago, Baltic, BlackForest, Coastal, Continental, CraterLake, Fortress, GoldRush, Highland, Islands, Mediterranean, Migration, Nomad, Rivers, TeamIslands |
| `--size S`, `--seed N` | tiny, small, medium, normal, large, giant; the map's seed |
| `--resources R`, `--age A`, `--reveal V` | Starting resources, age and map visibility |
| `--ai-only`, `--no-ai` | The computer plays every seat, or none |
| `--host PORT --players N`, `--join HOST:PORT` | A network game without the lobby, started once N people are in |
| `--windowed`, `--fullscreen`, `--widescreen`, `--mute`, `--no-movies` | |

The controls are the original's: left-click and drag to select, right-click to
order, the command panel and hotkeys for the rest, the arrow keys, the screen's
edges and the minimap to move the view. One thing the original did not have:
the mouse wheel zooms the view in and out, as do `=` and `-`. The original's
wheel hotkeys (the Town Center, the next idle villager) are on Ctrl and the
wheel.

## Checking changes

```sh
bash tools/check_determinism.sh
```

runs the headless self-check: a bit-exact determinism hash and dozens of
scenarios covering movement, economy, combat, the computer players, networking,
water, trade, diplomacy, scenarios and the editor. See
[docs/VERIFY.md](docs/VERIFY.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the engine is put together
- [docs/DATA.md](docs/DATA.md): the game files it reads, and how they are prepared
- [docs/VERIFY.md](docs/VERIFY.md): the self-check
- [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md): lockstep networking
- [android/README.md](android/README.md): the Android build and touch controls
- [switch/README.md](switch/README.md): the Switch build, the SD card and the controller
- [ROADMAP.md](ROADMAP.md): what's next, and known issues

## AI-assisted development

I didn't build this alone, and it would be a lie to say so. I worked on OpenAoK
with Claude, and it wrote a big share of the code, the tools and these
docs. My part was knowing what the game should feel like: deciding what to
build next, and how to build it, and let Claude do what it does best.
[CLAUDE.md](CLAUDE.md) is the guide it works from, if you'd like to work on the
project the same way.

## License and credits

OpenAoK is released under the [MIT license](LICENSE). The Indeo 5 movie
decoder follows FFmpeg's and is under the LGPL 2.1 or later, and the vendored
libraries keep their own licenses: see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Thanks to the [openage](https://openage.dev) project for its documentation of
the game's file formats and mechanics, and to
[genieutils](https://github.com/sandsmark/genieutils) for reading the dat.

## Disclaimer

OpenAoK is a free, non-commercial fan project. It is not affiliated with,
endorsed by or sponsored by Microsoft or Nintendo, and it contains no game
assets: you need your own copy of the game to play.

*Age of Empires II: The Age of Kings* © Microsoft Corporation. *Age of Empires*
is a trademark of Microsoft Corporation. Nintendo Switch is a trademark of
Nintendo. The screenshots in this README were
created under Microsoft's
[Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules) using
assets from *Age of Empires II: The Age of Kings*, and they are not endorsed by
or affiliated with Microsoft.
