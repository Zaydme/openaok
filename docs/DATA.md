# Game data

OpenAoK plays from the data of *Age of Empires II: The Age of Kings* (1999),
read from your own copy of the game. None of it is in this repository.

## What you need

The files of the original Age of Kings CD (version 1.0), copied into
`original_game_1999/` at the top of the repository -- the whole CD, or a
mounted image of it. The Conquerors expansion and the HD and Definitive
editions use different data and are not supported.

| Path | Holds | Needed |
|---|---|---|
| `GAME/EMPIRES2.CAB` | The installed game: the DRS archives, campaigns, sounds, taunts, history | yes |
| `GAME/DATA/EMPIRES2.DAT` | The rules: units, technologies, effects, civilisations, terrains, graphics, sounds | yes |
| `GAME/LANGUAGE.DLL` | Every string the game shows | yes |
| `GAME/EMPIRES2.EXE` | The default hotkeys | yes |
| `SYSTEM/FONTS` | The interface's fonts | yes |
| `GAME/AVI` | The start-up and campaign movies | optional |
| `GAME/SOUND` | The music and the campaigns' narration | optional |
| `GAME/CAMPAIGN` | The campaigns' pictures | optional |
| `GOODIES/DEMO.SCN` | The bonus scenario | optional |

Names are matched in any case and renamed to the CD's upper case.

Arial, which the CD leaves to Windows, comes from the machine: macOS's own, or
the Microsoft core fonts or Liberation Sans on Linux.

## Preparing it

```sh
python3 tools/data/prepare_data.py [--force]
```

`build.sh` and the Android build run it themselves. It:

1. checks the files above;
2. unpacks `GAME/EMPIRES2.CAB` into `assets/original/` with `cabextract`;
3. writes the palette (interfac.drs entry 50500) to
   `assets/original/interfac_extracted/50500.bina`;
4. builds the dat dumper (`tools/data/dump_dat.cpp`, against
   `third_party/genieutils`) and checks the dat is the 1999 data (`VER 5.7`);
5. runs the extractors below, which write `assets/extracted_data/` and
   `assets/sprites/terrain/blendmasks.png`.

It then writes `assets/.prepared`, and does nothing on later runs unless the
CD's files change or `--force` is given. Everything it writes is ignored by git.

| Script | Writes | Holds |
|---|---|---|
| `extract_tables.py` | `graphics.json`, `units.json` | The dat's graphic table and Gaia's unit table |
| `extract_language.py` | `strings.json` | LANGUAGE.DLL's strings by id |
| `extract_rules.py` | `rules.json`, `media.json` | Units' numbers (attack and armour by class, speed, range, costs, storage, tasks), technologies, effects, civilisations, terrain restrictions; graphics' deltas and sounds; the sound table |
| `extract_terrain_data.py` | `terrain.json` | Terrain art, minimap colours, blend priorities, map-script constants, map objects' art and stock, player colours |
| `extract_interface_data.py` | `interface.json` | Icons, command-panel slots, names and help text |
| `extract_animation_data.py` | `animations.json` | Attack and work graphics, frame timing |
| `extract_slp_shadows.py` | `shadows.json` | Each graphic's separate shadow graphic |
| `extract_annexes.py`, `generate_annex_overrides.py` | `annexes.dat.json`, `annexes.overrides.json` | Buildings made of several pieces, and each piece's offset |
| `extract_building_art.py` | `buildings.json` | Building art per architecture set and age |
| `extract_blendmasks.py` | `blendmasks.png` | The terrain blending masks |
| `extract_ai.py` | `ai.json` | Villager targets and fishing figures from the computer players' scripts |
| `extract_techtree.py` | `techtree.json` | The tech tree screen's layout |
| `extract_hotkeys.py` | `hotkeys.json` | The default hotkeys |
| `extract_connections.py` | `connections.json` | The TCP/IP connection's name in the multiplayer menu, from `DIRECTX/DPLAY.INF` |

## Read at run time

| File | Holds |
|---|---|
| `assets/original/Data/graphics.drs` | Every sprite (SLP) |
| `assets/original/Data/terrain.drs` | Terrain tiles |
| `assets/original/Data/interfac.drs` | Interface art, the palette, interface sounds |
| `assets/original/Data/sounds.drs` | Unit and combat sounds |
| `assets/original/Data/gamedata.drs` | Random map scripts and the computer players' rule scripts (`.per`) |
| `assets/original/Data/TileEdge.Dat`, `BlkEdge.Dat` | The fog of war's edge masks |
| `assets/original/Campaign`, `Scenario`, `Sound`, `Taunt`, `History` | Campaigns, scenarios, campaign sounds, taunts, the History screen's texts |
| `original_game_1999/GAME/AVI`, `GAME/SOUND`, `GAME/CAMPAIGN`, `SYSTEM/FONTS`, `GOODIES` | Movies, music, campaign media, fonts, the bonus scenario |
| `assets/extracted_data/*.json` | The tables above |

## Conventions in the data

- **Resource ids are the dat's.** Stone is 2 and gold 3 -- the reverse of the
  stockpile order -- and costs, storage and effects share that numbering
  (`Rules::DatResource`).
- **Every player plays from its own copy of the unit rules.** Effects change
  the copy; an upgrade keeps the slot's id and takes the new unit's numbers and
  art.
- **A villager at work is a different unit** (`VMLUM` for a lumberjack ...),
  with its own graphics, gather rate and sounds.
- **Walkability is a terrain restriction.** Each mover's restriction says which
  terrains it may cross; the grid keeps one passability layer per distinct
  restriction. Buildings and map objects are placed by theirs.
- **Fish store resource 17**, which the fishing ship's gather task turns into
  food.
- **A gather task's work value multiplies the gatherer's rate.**
- **A tree is felled before it is cut.** The lumberjack's gather task (110)
  names a proceeding graphic, the felling stroke, and a working graphic,
  chopping the trunk on the ground. The felled tree is drawn as its dying
  graphic, in one of its shapes, and keeps its wood; when that runs out it
  becomes its dead unit, the stump.
- **A fish trap is a farm with no foundation terrain.**
- **A transport carries whatever has a Garrison task naming its class (20).**
- **A trader's task names where it trades**: the trade cart the Market, the cog
  the Dock.
- **The dat's axes are a quarter turn from the engine's.** A piece the dat
  places at (dx, dy) from a building's middle stands at (dy, -dx) on the grid;
  scenario positions and cliff pieces are in the dat's frame too.
- **A wall's piece is chosen by its neighbours** (the dat's adjacent mode): a run
  along either axis, a post, or a diagonal.
- **A gate is a placing unit** that stacks an arch between two posts, in four
  orientations; its arch's transform unit is the gate opened.
- **The trebuchet is a building-type unit**, which is why it can be repaired.
- **Sounds hang on graphics**: a dying graphic carries its death cry; attack and
  work graphics carry sounds on particular frames.
