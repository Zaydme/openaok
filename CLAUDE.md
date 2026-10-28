# OpenAoK

A from-scratch C++20 / SDL 2 engine for *Age of Empires II: The Age of Kings*
(1999) that plays from the user's own copy of the CD. Gameplay and looks must
match the original; the code underneath is our own. Read
`docs/ARCHITECTURE.md` for the module map and `docs/DATA.md` for the data.

## Build, run, check

```sh
./build.sh                       # prepares the game data on first run, then builds (Release)
cd build && ./OpenAoK            # always run from build/: data paths are relative to it
bash tools/check_determinism.sh  # the gate: must pass before and after every change
```

- The game data is prepared from `original_game_1999/` (the CD) by
  `tools/data/prepare_data.py` into `assets/original/`, `assets/extracted_data/`
  and `assets/sprites/`. All of it is git-ignored. **Never commit game files or
  anything extracted from them.**
- `AOE_VERIFY_ONLY=name1,name2 ./OpenAoK --verify 10` runs single self-check
  scenarios; `AOE_AI_TRACE=1` traces the computer players.
- Update `GOLDEN` in `tools/check_determinism.sh` only when a simulation change
  is intended, after confirming the new hash twice.
- Android: `cd android && ./fetch_deps.sh && ./gradlew assembleRelease` (JDK
  21, SDK platform 36, NDK 28.2). See `android/README.md`.
- Switch: `switch/build.sh` (devkitPro, or its Docker image) and
  `python3 switch/package.py` for the SD card's folder. See `switch/README.md`.

## Captures

`--screenshot PATH` renders and exits; use `--windowed` (a fullscreen capture
takes over the screen). Useful with it: `--game`, `--menu NAME`, `--clicks
"x,y;key:return;text:words;right:x,y;down:x,y;move:x,y;up:x,y"`, `--advance SECONDS`, `--look X Y`,
`--select NAME`, `--place NAME` and `--spawn NAME` (a building or a unit beside the
start; `NAME@P` is player P's, 0 Gaia's), `--frame-width N` (a frame of another screen's shape),
`--resolution 800x600|1024x768|1280x1024`. A click script that ends on `move:`
shows the game's cursor there. Flags are parsed in `src/main.cpp`.

## Rules

- **Data-oriented, never object-oriented.** Components are plain structs (EnTT);
  behaviour is free functions in modules, one module per concern. No entity
  classes, virtual dispatch for game objects, or getters wrapping fields.
- **Deterministic simulation.** 32.32 fixed point (`Core::Fixed32`) on a fixed
  16 ms tick. No `float`, `double` or `ToFloat()` in `src/ecs`,
  `src/pathfinding`, `src/physics` or `src/ai` (the gate checks). Randomness only
  from the world's seeded generators. Iterate in a defined order. Anything that
  moves keeps `previous_position` for interpolation.
- **One-way presentation.** Rendering, audio and the interface read the
  simulation and never write it. Orders reach it only as commands
  (`core/Command.h`) through `ecs/Orders`, which checks ownership.
- **Units are not on the grid.** Positions are continuous; "in reach" is a
  distance. Routes go through `Navigation::Route`; footprints through
  `ecs/BuildingGeometry`; standing spots through `pathfinding/ApproachPlanner`.
- **Read figures from the data**: the dat, the DRS archives, LANGUAGE.DLL, the
  map and AI scripts. Extend `tools/data/dump_dat.cpp` rather than writing a new
  dat parser. A figure that is not in the data is a named constant in the module
  that uses it.
- **Interface text comes from LANGUAGE.DLL** (`Core::Strings`), never typed in.
- **Prove changes.** New behaviour gets a check in `src/verify/` (one scenario
  per file, its own world from a fixed seed, one machine-readable output line),
  and the check is seen to fail once with the bug it guards against.

## Rendering

- Sprites come from the SLPs in `graphics.drs` through `HAL::GraphicCache`, one
  atlas per graphic; a `Sprite` names `slp_id` + `slp_frame`, never a file.
- Anchor by each frame's hotspot; never guess an anchor. Multi-piece buildings
  are placed from the annex tables.
- Depth is `world_x + world_y`, compared exactly, then `sort_bias`, texture,
  entity id. Do not band depths.
- Shadows are separate shadow graphics (`shadows.json`); player colour is a
  shade into the player's ramp (from the dat), clamped to the ramp.

## Performance

- No allocation or string building in per-frame loops; reserve up front.
- Decide categories once at spawn and store them on components.
- Cull before building render state. Keep the depth sort's texture tie-break,
  which lets SDL batch.
- Measure with a Release build, the same seed and camera, vsync off.

## Data conventions

- Dat resource ids: stone is 2, gold 3 (`Rules::DatResource` converts).
- Each player plays from its own copy of the unit rules (`Players::UnitOf`); an
  entity's rules are `Players::RulesFor` (a converted unit keeps its maker's).
- An upgrade keeps the entity's `Identity` (the base unit id).
- The dat's axes are a quarter turn from the grid's: (dx, dy) in the dat is
  (dy, -dx) here (`BuildingGeometry::FromDatAxes`).
- A garrisoned unit has no `Transform`.

## Comments and docs

Comment what code does where it is not obvious: summaries of modules and
functions, units and coordinate frames, what data values mean. Keep comments
short. No development history, debate or provenance (how something was found)
in comments or docs.
