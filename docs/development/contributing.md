# Contributing

Thanks for helping. OpenAoK plays *The Age of Kings* as the 1999 game plays and
looks, on code of its own, so every change answers two questions: does the
game still behave as the original does, and does the code still keep the
engine's rules? This page gives those rules and the reasons for them.

Build the game first ([Building from source](/development/)), and read the
[Overview](/engine/) for the map of the modules.

## How a change goes

1. Run `bash tools/check_determinism.sh` before you start, so you know the
   gate passes on your machine.
2. Make the change in the module whose concern it is, rather than in its
   caller.
3. Give new behaviour a self-check in `src/verify/`, and see it fail once with
   the bug it guards against (see [Self-checks](/development/verification)).
4. Run the gate again. If the change is meant to alter the simulation and the
   golden hash moves, confirm the new hash on a second run and update `GOLDEN`
   in `tools/check_determinism.sh`.
5. Open a pull request. History and debate belong there, not in the code.

::: warning Never commit the game
The repository holds no game files and nothing extracted from them:
`original_game_1999/`, what is prepared from it in `assets/`, and the data
folders made for phones and the Switch are all ignored by git. Keep it so.
:::

## Match the original

Gameplay and looks match the original: the rules, the timings, the screens,
the panels, the cursors, the messages and the hotkeys. Where OpenAoK adds
something the original does not have (zoom, widescreen, no selection limit,
populations past 200 and the rest), it is an addition listed in
[Improvements on the original](/guide/improvements), never a quiet change to
how the game plays.

## Read figures from the data

Every figure the original game states is read from its data: the dat, the DRS
archives, LANGUAGE.DLL, the random map scripts and the AI scripts. Look there
before adding a constant. To read more of the dat, extend
`tools/data/dump_dat.cpp` rather than writing another dat parser. When a
figure is not in the data, make it a named constant in the module that uses
it, with a comment saying what it is:

```cpp
// How far a stick leans before it counts, of its full reach: a stick at rest
// is seldom quite at its middle.
constexpr float kDeadZone = 0.15f;
// The pointer's speed with the stick pushed all the way, in screen heights a
// second.
constexpr float kPointerSpeed = 1.4f;
```

[The game's data](/engine/data) describes what each file holds and how it is
prepared.

## Interface text from LANGUAGE.DLL

Every word the interface shows comes from LANGUAGE.DLL through
`Core::Strings`, by string id, never typed into the code. The out-of-sync
banner of a network game, for example, is
`Core::Strings::Get(Core::Strings::Id::OutOfSync)`. The fonts are the CD's
own, as LANGUAGE.DLL names them.

## Data-oriented design

The engine is data-oriented, never object-oriented. Components are plain
structs (EnTT); behaviour is free functions in modules, one module per concern.
There are no entity classes, no virtual dispatch for game objects, and no
getters wrapping fields.

A component is data and nothing else:

```cpp
struct Health {
    int current_hp = 100;
    int max_hp = 100;
    // Damage taken below a whole hit point, carried to the next blow. Hit
    // points are kept as fractions: a blow struck down a hill does a quarter
    // more, 3.75 from a villager's 3, and the three quarters count.
    Core::Fixed32 wound{};
};
```

and a module is a namespace of functions over the registry, such as
`ECS::Units`:

```cpp
entt::entity Spawn(entt::registry& registry, int unit_id, Core::PlayerId player,
                   Components::Vector2D at);
// ...
void Become(entt::registry& registry, entt::entity unit, int unit_id);
```

Decide what kind of thing an entity is once, when it is made, and store it on
its components; the systems then iterate views of those components. See
[Entities and systems](/engine/simulation) and
[Performance](/development/performance).

## Determinism

The simulation is deterministic: the same orders on the same ticks give the
same world, bit for bit, on every machine. Lockstep multiplayer, saved and
recorded games and the golden hash all depend on it.

- **Fixed point.** The simulation runs on 32.32 fixed point (`Core::Fixed32`)
  on a fixed 16 ms tick. No `float`, `double` or `ToFloat()` in `src/ecs`,
  `src/pathfinding`, `src/physics` or `src/ai`; the gate searches the sources
  for them. Fractional figures from the data are turned into fixed point once,
  as they are loaded. Rendering and audio may use floats.
- **Seeded randomness.** Randomness comes only from the generators held in the
  world (`Core::Random`), never from `rand()`, a global or the clock.
- **A defined order.** Iterate in an order that is the same on every machine,
  such as the order of entity ids, never the order of a hash table or of
  pointers.
- **Interpolation.** Anything that moves keeps `previous_position` in its
  `Transform`, so the renderer can draw it between ticks.

See [Fixed point and determinism](/engine/determinism).

## One-way presentation

Rendering, audio and the interface read the simulation and never write it.
Orders reach the simulation only as commands (`core/Command.h`), each carrying
the player who gave it. The interface and the network give orders this way;
the computer players and a scenario's triggers give the same orders from
inside the tick. `ecs/Orders` carries every one out after checking that the
player owns what it orders about.

## Units are not on the grid

The tile grid is for buildings, terrain and the path search. A unit's position
is a continuous fixed-point point: never snap it to a tile, and measure reach
as a distance. Routes go through `Navigation::Route`, footprints through
`ecs/BuildingGeometry`, and the spots units stand at round a target through
`pathfinding/ApproachPlanner`. See [Movement and routes](/engine/pathfinding).

## Conventions in the data

- The dat's resource ids put stone at 2 and gold at 3; convert with
  `Rules::DatResource`, never by casting.
- Each player plays from its own copy of the unit rules (`Players::UnitOf`).
  An entity's rules are `Players::RulesFor`: its owner's, or for a converted
  unit its maker's.
- An upgrade keeps the entity's `Identity`, the base unit id; the owner's
  rules for that slot change instead.
- The dat's axes are a quarter turn from the grid's: (dx, dy) in the dat is
  (dy, -dx) here (`BuildingGeometry::FromDatAxes`).
- A garrisoned unit has no `Transform`.

## Rendering

- Sprites come from the SLPs in `graphics.drs` through `HAL::GraphicCache`. A
  `Sprite` names `slp_id` and `slp_frame`, never a file.
- Anchor every frame by its hotspot; never guess an anchor. Buildings of
  several pieces are placed from the annex tables.
- Depth is `world_x + world_y`, compared exactly, then `sort_bias`, the
  texture and the entity id. Do not band depths.
- Shadows are separate shadow graphics (`shadows.json`). Player colour is a
  shade into the player's colour ramp from the dat, clamped to the ramp.

See [The frame](/engine/rendering/) and
[Sprites and texture pages](/engine/rendering/sprites).

## Performance

No allocation or string building in per-frame loops; decide categories once
at spawn; cull before building render state; keep the depth sort's texture
tie-break, which lets SDL batch. Measure with a Release build, the same seed
and camera, and vsync off. [Performance](/development/performance) explains
each.

## Prove it

New behaviour comes with a check in `src/verify/`: one scenario per file,
building its own world from a fixed seed, printing one machine-readable line
that the gate reads. The check is seen to fail once, with the bug it guards
against put back, before it is trusted to pass. For a change to what the
player sees, compare it with the original's screens:
`--screenshot PATH --windowed` renders a frame to a file and exits (see
[Command line](/guide/command-line)). See [Self-checks](/development/verification).

## Comments and docs

Comment what the code does where that is not obvious from the code: summaries
of modules and functions, units and coordinate frames, what data values mean.
Keep comments short. Write comments and docs in the present tense, about the
code as it is: no development history, no debate, and no account of how
something was found. British spelling, as the code uses: colour,
civilisation, centre.

## Where it lives in the code

- [`CONTRIBUTING.md`](https://github.com/Zaydme/openaok/blob/main/CONTRIBUTING.md): these rules in short
- [`tools/check_determinism.sh`](https://github.com/Zaydme/openaok/blob/main/tools/check_determinism.sh): the gate
- [`src/verify/Harness.h`](https://github.com/Zaydme/openaok/blob/main/src/verify/Harness.h): every self-check
- [`src/core/Command.h`](https://github.com/Zaydme/openaok/blob/main/src/core/Command.h): the commands
- [`src/ecs/Orders.h`](https://github.com/Zaydme/openaok/blob/main/src/ecs/Orders.h): orders checked and carried out
- [`src/core/FixedPoint.h`](https://github.com/Zaydme/openaok/blob/main/src/core/FixedPoint.h): `Core::Fixed32`
- [`src/core/Strings.h`](https://github.com/Zaydme/openaok/blob/main/src/core/Strings.h): LANGUAGE.DLL's text by id
- [`tools/data/dump_dat.cpp`](https://github.com/Zaydme/openaok/blob/main/tools/data/dump_dat.cpp): the dat's reader
