# Architecture

OpenAoK is written from scratch in C++20 on SDL2. It contains no code from the
original game; it reads the original's data files and implements their rules.

## Principles

**Data-oriented.** Components are plain structs of data (EnTT, vendored in
`third_party/entt`); behaviour lives in free functions in modules that act on
them. There are no entity classes, no virtual dispatch for game objects and no
getters wrapping fields.

**A deterministic simulation.** The simulation runs on 32.32 fixed point
(`Core::Fixed32`) on a fixed 16 ms tick, draws randomness only from seeded
generators held in the world, and iterates in a defined order. The same orders
on the same ticks produce the same world, bit for bit, on every machine; lockstep
multiplayer and the self-check depend on it. Floating point is not allowed inside
the tick (`src/ecs`, `src/pathfinding`, `src/physics`, `src/ai`), and the gate
checks for it.

**One-way presentation.** Rendering, audio and the interface read the
simulation; the simulation never reads them. Orders reach the simulation only as
commands (`core/Command.h`), each stamped with the issuing player and checked by
`ecs/Orders` against what that player owns.

**The data decides.** Every figure the dat, the archives or LANGUAGE.DLL state is
read from them. The few figures the data does not hold are constants in the
module that uses them.

**Continuous positions.** Units stand and move at fixed-point positions. The
tile grid holds building footprints, terrain and the path search; nothing a unit
does is snapped to it.

**Passability per terrain restriction.** The grid keeps one passability layer
for each set of terrains a mover's restriction allows -- land (layer 0), the sea,
wild animals -- each with its connected regions. `ecs/Navigation` answers which
layer a unit moves on, and every route goes through `Navigation::Route`. A layer
carries the asking player's key in its upper bits (`Physics::Keyed`), so a gate
is open to its owner and allies and shut to everyone else.

## Layout

| Directory | Role |
|---|---|
| `src/core/` | Types, fixed point, the lobby (`GameSetup`), commands, the loaded tables (`Rules`, `Media`, `Strings`, `ConfigManager`), map generation (`RMSParser`, `MapGenerator`, `LandGenerator`, `Cliffs`, `Elevation`), input |
| `src/ecs/` | The simulation: `Simulation` owns the registry and runs the tick; one module per concern |
| `src/ecs/components/` | Plain data, one concern per file |
| `src/ecs/systems/` | Bulk systems: movement, workers, animation |
| `src/ai/` | Computer players |
| `src/net/` | Lockstep multiplayer, the lobby and finding games, the wire format |
| `src/physics/` | The tile grid: terrain, walkability per passability layer, occupancy |
| `src/pathfinding/` | A* over connected regions per layer, and the approach planner |
| `src/rendering/` | The SDL2 draw layer |
| `src/audio/` | Sound, beside rendering |
| `src/screens/` | Menus, the game setup screen and the multiplayer lobby, in-game dialogs |
| `src/editor/` | The scenario editor |
| `src/scenario/` | Scenario (.scn) and campaign (.cpn) files: reading and writing |
| `src/save/` | Saved games and recorded games |
| `src/hal/` | Platform layer: window, start-up and folders per machine, touch and controller input played as the mouse, the system keyboard, DRS archives and SLP decoding, texture atlases, audio device, sockets, the AVI reader and Indeo 5 decoder |
| `src/verify/` | The headless self-check, one scenario per file |
| `tools/data/` | Prepares the game data from the original CD |
| `android/` | The Android build |
| `switch/` | The Nintendo Switch build and its SD card folder |

## The tick

`ECS::Simulation::Update` runs, in order:

1. **Orders** -- every command queued for this tick, through `ecs/Orders`.
2. **Computer players** (`ai/Brain`) -- each runs a pass of its rule scripts and
   its executor, giving ordinary orders through `ecs/Orders`.
3. **Packing** -- siege engines setting up or packing.
4. **Movement** -- routes, separation, arrival.
5. **Workers** -- gathering, carrying, building, repairing.
6. **Garrisons, standing orders, monks, relics, animals.**
7. **Combat**, **missiles** in flight, **mortality** (dying units, corpses,
   carcasses).
8. **Buildings** finishing, **gates** opening and closing, **production** queues.
9. **Animation**, a scenario's **triggers**, **victory**, then **vision**.

Every tick also writes the **event log** (`ecs/Events`): units born, blows
struck, buildings finished, technologies researched, deaths, conversions. The
simulation only writes it; sound, the interface and the checks read it.

## Modules

| Module | Owns |
|---|---|
| `ecs/Players` | Each player's resources (the dat's whole table), its own copy of the unit rules, its art and technology statuses |
| `ecs/Effects`, `ecs/Techs` | Technologies' effects on a player's copy of the rules; research progression and the ages |
| `ecs/Buildings`, `ecs/BuildingGeometry` | A building's life -- placement, completion, destruction, art -- and its footprint |
| `ecs/Production`, `ecs/Population` | Unit and research queues; population and housing |
| `ecs/Units` | Assembling a unit (`Spawn`) and turning one into another (`Become`) |
| `ecs/VillagerJobs`, `ecs/DropSites`, `ecs/Resources` | The job unit a villager becomes at work; where loads may be dropped; what a depleted resource leaves |
| `ecs/Combat`, `ecs/Missiles`, `ecs/Mortality` | Attack and armour by class; missiles; death |
| `ecs/Formations`, `ecs/Duties` | Group moves; patrol, guard and follow |
| `ecs/Garrisons`, `ecs/Monks`, `ecs/Relics`, `ecs/Animals` | Garrisons, the town bell and transport ships; conversion and healing; relics; grazing, herding and fleeing |
| `ecs/Navigation` | A unit's passability layer, its routes and region, the nearest reachable ground |
| `ecs/Walls` | Walls laid as dragged lines, each segment's piece chosen by its neighbours; gates and locking |
| `ecs/Repairs` | Who may repair what, the pace, the cost per hit point |
| `ecs/Packing` | Siege engines packing and unpacking |
| `ecs/Cheats` | The cheat codes typed in chat |
| `ecs/Allegiance` | Changing an entity's owner; a converted unit keeps its maker's rules (`RulesOf`) |
| `ecs/Vision` | Each player's visible and explored ground |
| `ecs/Fog` | What each player keeps of what it does not see: pictures of buildings and map objects, the ground as last seen |
| `ecs/Victory` | Conquest, Wonders and relics; who is defeated and who has won |
| `ecs/Spies` | Spies' price and the enemy sight it gives; Treason, which marks the enemy Kings in Regicide |
| `ecs/Market`, `ecs/Trade`, `ecs/Tribute` | Commodity prices and trading; trade carts and cogs; tribute and its fee |
| `ecs/Chat`, `ecs/Flares` | Chat lines and flares, carried in lockstep, written to a log the interface reads |
| `ecs/Diplomacy` | Stances between players, allied victory, Lock Teams, team bonuses |
| `ecs/Score` | The score and the Achievements figures |
| `ecs/ScenarioWorld`, `ecs/Triggers` | A scenario's world, and its triggers, evaluated once a second |
| `ecs/Events`, `ecs/Digest` | The event log; a digest of the world for desync detection |
| `scenario/Scenario`, `scenario/Campaign`, `scenario/Writer` | Scenario files (1.18) and campaigns as plain data; writing scenarios back |
| `editor/` | The scenario editor: `Editor` (the screen), `Document` (edits to the file and its world), `Panels` and `TriggerPanel` (the tabs), `Ui` (immediate-mode controls). It never ticks the simulation |

Conventions the modules share:

- **A garrisoned unit has no `Transform`.** Removing the component takes the
  unit out of every view -- drawing, sight, movement, targeting.
- **An upgrade keeps identity.** `Identity` holds the base unit id for an
  entity's whole life; the owner's rules for that slot change with research.
- **An entity's rules come from `Players::RulesFor`**: its owner's table, or for
  a converted unit its maker's.
- **Footprints use the dat's clearance turned a quarter**: a piece the dat places
  at (dx, dy) from a building's middle stands at (dy, -dx) on the grid
  (`BuildingGeometry::FromDatAxes`).
- **Hostility is diplomacy's.** `Combat::Hostile` and `Combat::Friendly` read
  `ecs/Diplomacy`; victory, shared sight and tribute ask the same table.

## Maps

`core/RMSParser` reads a random map script from `gamedata.drs`, with its
includes and constants. `core/MapGenerator` runs the passes: lands
(`core/LandGenerator`), terrain clumps, beaches, cliffs (`core/Cliffs`), objects
placed by the script's rules, then hills (`core/Elevation`). Cliffs and hills use
their own random streams. `MapGenerator::SetUpLayers` builds the grid's
passability layers from the dat first.

## Presentation

`rendering/` draws the simulation's state each frame, interpolating positions
between ticks. Sprites come straight from the SLPs in `graphics.drs` through
`hal/GraphicCache`, one atlas per graphic; player colour, shadows and outlines
are the SLP's own pixel classes. Terrain is drawn in chunks and blended with the
blendomatic masks. The fog of war is a checkerboard over explored ground with
the TileEdge and BlkEdge masks at its edges.

The interface uses the original interface art and LANGUAGE.DLL's text, in the
CD's fonts rasterised by FreeType. The game draws into a frame of one of the
three original screen sizes (or a widescreen frame of that height), scaled to the
window. In a widescreen frame the top bar and the bottom panel reach the frame's
edges; each can be drawn larger (`UIRenderer::HudSpace`).

`audio/` reads the same state: sounds tied to graphics (a death cry, a sword's
ring on a given frame), unit voices, alarms, chat and taunts. `hal/Audio` mixes
the original WAVs, and MP3s through minimp3.

The start-up movies are the original AVIs, read by `hal/Avi` and decoded by
`hal/Indeo5`, an Indeo Video 5 decoder.

## Computer players

The computer players are the original rule scripts -- the `.per` files in
`gamedata.drs` -- run by the engine:

- **The rules.** `ai/Script` reads the scripts (`defrule`, `defconst`, `load`,
  `load-random`, conditional loading) and compiles them to flat arrays per
  computer seat, under the symbols the lobby defines (`DIFFICULTY-HARD`,
  `ARABIA-MAP`, ...). `ai/Language` holds the language's facts, actions,
  parameters and strategic numbers. `ai/Interpreter` runs a rules pass once a
  second; `ai/Facts` and `ai/Actions` evaluate them against the world; `ai/Mind`
  holds goals, strategic numbers, timers and escrow.
- **The executor** carries out what the rules ask: `ai/Economy` (villagers,
  builders, farms, hunting), `ai/Placement` (where buildings go), `ai/Walls`
  (the two perimeters, walls and gates), `ai/Military` (attack groups,
  defence), `ai/Navy`, `ai/Scouting`, behind `ai/Executor`.

Computer players act only through `ecs/Orders`, inside the tick, on their own
random streams, so every machine in a network game runs them identically.

## Multiplayer

See [MULTIPLAYER.md](MULTIPLAYER.md).
