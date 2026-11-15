# AgeOfEmpire2-openaok — Worldcraft

[Русская версия](readme_ru.md)

## Terrain and earthworks

An extension of [OpenAoK](https://github.com/Zaydme/openaok) in C++20: villagers
plant trees individually, in a line, or across a rectangular area. Features
include a preview while dragging, Shift queuing, shared work, resource costs,
cancellation, growth, and normal woodcutting once the forest has grown.
Single-player, network commands, saves, and replays are supported. In
Worldcraft 0.5.0, villagers also create tall mountains with slopes, dig dry
moats, ditches, and lakes, and restore the original terrain.
Each task supports lines and areas; gentle slopes are traversable, while
steep slopes and moats block movement. Three mountain heights are available.
Obstacles move to a free location, and dry terrain work replaces water with
sand; building foundations remain intact even when work takes place right
next to them.

<p align="center">
  <img src="docs/public/images/worldcraft/mountains.png" width="820" alt="A mountain with slopes created by villagers">
</p>

*Mountains: villagers raise the terrain and shape its slopes.*

<p align="center">
  <img src="docs/public/images/worldcraft/ditches.png" width="820" alt="A dry ditch dug near the town center">
</p>

*Ditches: villagers lower the terrain to create dry obstacles.*

## Trojan Horse

The package includes the Trojan Horse: a villager builds a land transport
for 12 villagers and infantry units. Its button is the third in the
villager's third command row; right-click your own horse to board it, and
use the transport's unload command to choose a destination.
Embarked archers and spearmen fire a combined volley while stationary;
both automatic fire and attack orders are supported. Spears have a range
of 4 tiles and no bonus against cavalry. Parameters, restrictions, and
compatibility: [transport](mods/worldcraft/transport/README.txt).

<p align="center">
  <img src="docs/public/images/worldcraft/trojan-horse.png" width="820" alt="The Trojan Horse land transport in the game">
</p>

*Trojan Horse: a land transport for villagers and infantry.*

## Gorynych

Gorynych flies over obstacles and attacks a single target with fire from
all three heads. Each new random map starts with 10 neutral dragons that
can be tamed by approaching within 3 tiles. A villager raises Gorynych
using the fourth button in the third row: 400 wood, 300 gold, 120 seconds,
and 5 population slots. Parameters and artwork:
[Gorynych](mods/worldcraft/gorynych/README.txt).

<p align="center">
  <img src="docs/public/images/worldcraft/gorynych.png" width="820" alt="The three-headed Gorynych breathing fire over a forest">
</p>

*Gorynych: flight over obstacles and fire from three heads.*

## Walking Tree

The mod adds a Walking Tree: a villager grows a controllable melee fighter.
Its button is the first in the villager's third command row. It requires
200 wood, 50 gold, 60 seconds of work, and 3 free population slots.
It has its own walking, attack, death, and remains animations; settings
are in `mods/worldcraft/treeman/unit.json`.

Each new random map starts with 10 neutral wild treants.
Bring a villager or fighter within three tiles: the treant joins you for
free and becomes a normal combat unit. Enemy units can no longer win it
over by approaching. A tamed treant uses 3 population slots even if the
current limit is already full. Villagers can still grow treants.

<p align="center">
  <img src="docs/public/images/worldcraft/treant.png" width="820" alt="A controllable Walking Tree beside the town center">
</p>

*Treant: a melee fighter grown or tamed by the player.*

## Rivers and water moats

Rivers and water moats are available through the second button in the
villager's third command row. Choose a line width of 1/3/5 tiles, then drag
out a river, moat, or area. Completed tiles fill with water; river flow runs
from the start of the line to its end and is shown visually.
Ships can use rivers that are wide enough; water moats block both land
units and ships. Restoration returns the original terrain. Other planned
creatures have not yet been implemented.

<p align="center">
  <img src="docs/public/images/worldcraft/water.png" width="820" alt="A man-made river with a ship and a separate water moat">
</p>

*Water: a navigable river and a water moat made by villagers.*

## Project status and packaging

- [Mod controls and settings](mods/worldcraft/README.txt).
- [Restoring the verified Windows working directory](local-support/README.txt).
- [Project status and verification results](local-support/CHECKPOINT.txt).

Checkpoint from November 11, 2026: targeted checks **20/20**, full run
**127/130**. Three existing failures remain: late-game AI development and
two checks without `DEMO.SCN`. [River verification](local-support/RIVERS-VERIFICATION.txt)
covers construction, sailing, filling in water, saves, replays, TCP, and an
observation of a single crash during automatic exit after a screenshot.
[Treant verification](local-support/TREEMAN-VERIFICATION.txt) covers taming,
growing, and combat.
[Windows packaging](windows/installer/README.txt) creates a local installer
that runs without development tools.

You need your own **The Age of Kings (1999)** game files to play. Original
game data, extracted assets, fonts, saves, and local builds are not included
in the repository. Network players must use the same build and
`mods/worldcraft` package. Saves from the previous version are not
automatically converted.

The original OpenAoK project description is preserved below. Its download
links refer to upstream releases, not builds of this mod.

---

<p align="center">
  <img src="docs/public/images/logo.png" width="680" alt="OpenAoK">
</p>

<p align="center">
  <b>A free, open-source engine for Age of Empires II: The Age of Kings, on your desktop and in your pocket.</b>
</p>

<p align="center">
  <a href="https://zaydme.github.io/openaok/"><b>Documentation</b></a> ·
  <a href="https://zaydme.github.io/openaok/guide/"><b>Getting started</b></a> ·
  <a href="https://github.com/Zaydme/openaok/releases"><b>Download</b></a>
</p>

<p align="center">
  <img src="docs/public/images/phone-town.png" width="820" alt="OpenAoK running on an Android phone">
</p>

## Why

Playing Age of Empires II on a phone has been a dream of mine since I was a
kid. Microsoft was never going to make that happen, so I did it myself.

OpenAoK is a new engine, written from scratch, that plays the original 1999
game from your own copy of its CD: the same units, maps, art, sounds, music,
movies and campaigns, on Windows, macOS, Linux, Android phones, iPhones and
iPads, and the Nintendo Switch.

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
  your CD into a folder, and OpenAoK prepares everything it needs from it.

## Screenshots

<table>
  <tr>
    <td><img src="docs/public/images/economy.jpg" alt="A computer player's town after fifteen minutes"></td>
    <td><img src="docs/public/images/battle.jpg" alt="Two armies meeting"></td>
  </tr>
  <tr>
    <td><img src="docs/public/images/town.jpg" alt="The bonus scenario"></td>
    <td><img src="docs/public/images/tech-tree.jpg" alt="The technology tree"></td>
  </tr>
  <tr>
    <td><img src="docs/public/images/campaign.jpg" alt="The William Wallace campaign map"></td>
    <td><img src="docs/public/images/achievements.jpg" alt="The Achievements timeline"></td>
  </tr>
</table>

### On a phone

<p align="center">
  <img src="docs/public/images/phone-build.png" width="49%" alt="Building on a phone, with the larger interface">
  <img src="docs/public/images/phone-start.png" width="49%" alt="A new random map game on a phone">
</p>

<p align="center">
  <img src="docs/public/images/iphone-game.png" width="820" alt="OpenAoK running on an iPhone">
</p>

## Controls

With a mouse and keyboard, the controls are the original's: left-click and
drag to select, right-click to order, the command panel and its hotkeys for the
rest. The mouse wheel also zooms the view, which the original could not.

On a touch screen the game is played as with a mouse, and it is surprisingly
comfortable: tap to click, hold to right-click, drag one finger to select, two
fingers to scroll the map, pinch to zoom. This is the same on Android phones,
iPhones and iPads. The top bar, the panel and the text can each be drawn
larger, and in widescreen the interface reaches the screen's edges.

On the Nintendo Switch the controller plays as a mouse and the game's hotkeys:

<p align="center">
  <img src="docs/public/images/switch-controls.svg" width="900" alt="The Switch's controls: the left stick moves the pointer, the right stick scrolls the map and with ZL held zooms, A and B are the left and right mouse buttons, plus is the game menu, minus is chat, and the other buttons are the game's hotkeys">
</p>

Every control, the options and what OpenAoK adds to the original are in the
guide: [Controls](https://zaydme.github.io/openaok/guide/controls),
[Options](https://zaydme.github.io/openaok/guide/options),
[Improvements on the original](https://zaydme.github.io/openaok/guide/improvements).

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
- **Victory** by conquest, Wonder or relics, and a scenario's score, time limit
  or custom conditions; Regicide and Death Match; diplomacy, allied victory,
  chat, taunts, flares and the cheat codes.
- **Presentation**: sprites straight from the original SLPs with player colours
  and shadows, terrain blending, fog of war, the original interface, sounds,
  music and the start-up movies.
- **Multiplayer** in lockstep over TCP, with desync detection: games on the
  local network found by themselves, others joined by address, and the lobby
  with its chat and ready checks.

What comes next, and the known issues, are on the
[Roadmap](https://zaydme.github.io/openaok/development/roadmap).

## How it works

A deterministic simulation on 32.32 fixed point at a fixed 60 Hz, so the same
orders give the same world on every machine, read by a renderer that draws the
original's sprites straight from its archives. The documentation explains it
all, from the CD's file formats to the computer players:

- [How the engine is put together](https://zaydme.github.io/openaok/engine/)
- [The game's data](https://zaydme.github.io/openaok/engine/data) and [random maps](https://zaydme.github.io/openaok/engine/random-maps)
- [Fixed point and determinism](https://zaydme.github.io/openaok/engine/determinism) and [lockstep multiplayer](https://zaydme.github.io/openaok/engine/multiplayer)
- The game: [units](https://zaydme.github.io/openaok/engine/units), [movement](https://zaydme.github.io/openaok/engine/pathfinding), [the economy](https://zaydme.github.io/openaok/engine/economy), [buildings](https://zaydme.github.io/openaok/engine/construction), [training and research](https://zaydme.github.io/openaok/engine/production), [combat](https://zaydme.github.io/openaok/engine/combat), [armies and the navy](https://zaydme.github.io/openaok/engine/war), [victory](https://zaydme.github.io/openaok/engine/victory), [scenarios](https://zaydme.github.io/openaok/engine/scenarios), [computer players](https://zaydme.github.io/openaok/engine/ai)
- [Rendering](https://zaydme.github.io/openaok/engine/rendering/), [the interface](https://zaydme.github.io/openaok/engine/interface) and [sound](https://zaydme.github.io/openaok/engine/audio)

## Getting started

OpenAoK needs **your own, legally obtained copy** of *Age of Empires II: The
Age of Kings* (1999, the original CD), not The Conquerors or the HD and
Definitive editions. [Your game files](https://zaydme.github.io/openaok/guide/game-files)
explains what to copy. The downloads hold none of the game's data; the data is
prepared from your CD.

| Platform | | |
|---|---|---|
| **Windows** 10 and 11, 64-bit | A folder to unzip; the data is prepared on first start | [Guide](https://zaydme.github.io/openaok/guide/windows) |
| **macOS and Linux** | Built from source in one step | [Guide](https://zaydme.github.io/openaok/guide/desktop) |
| **Android** 10 or later, arm64 | An APK, with a data folder made on a computer | [Guide](https://zaydme.github.io/openaok/guide/android) |
| **iPhone and iPad**, iOS 15 or later | An IPA to sideload, or built with Xcode | [Guide](https://zaydme.github.io/openaok/guide/ios) |
| **Nintendo Switch**, homebrew | An `.nro` and a data folder on the SD card | [Guide](https://zaydme.github.io/openaok/guide/switch) |

Downloads are on the [releases](https://github.com/Zaydme/openaok/releases)
page. Help with problems is in
[Troubleshooting](https://zaydme.github.io/openaok/guide/troubleshooting).

## Building and contributing

[Building from source](https://zaydme.github.io/openaok/development/) covers
every platform's build. Every change is checked by a headless self-check, a
bit-exact determinism hash and over a hundred scenarios
([Self-checks](https://zaydme.github.io/openaok/development/verification)), and
[Contributing](https://zaydme.github.io/openaok/development/contributing) says
how to work on the project.

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
