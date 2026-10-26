# Contributing

Thanks for helping. Build the game first (see the README), then keep to the
rules below.

## Read figures from the data

Every figure the original game states is read from its data: the dat, the DRS
archives, LANGUAGE.DLL, the random map and AI scripts. Look there before adding
a constant. When a figure is not in the data, put it as a named constant in the
module that uses it. Never add the game's files, or anything extracted from
them, to the repository.

## Data-oriented design

Components are plain structs. Behaviour is free functions in modules, one module
per concern. Give new logic a home in the module whose concern it is, rather
than in its caller.

## Determinism

No floating point inside the simulation tick (`src/ecs`, `src/pathfinding`,
`src/physics`, `src/ai`); the gate checks the source for it. Randomness comes
from the generators held in the world, never a global or `rand()`. Iterate in a
defined order. Rendering and audio may use floats.

## Units are not on the grid

The tile grid is for buildings, terrain and the path search. A unit's position
is a continuous fixed-point point: never snap it to a tile, and measure reach as
a distance. Route through `Navigation::Route`.

## Orders go through `ecs/Orders`

The interface, the computer players and the network give orders as commands
carrying the issuing player, carried out inside the tick by `ecs/Orders`.
Nothing outside the simulation writes to it.

## Check it

Run `bash tools/check_determinism.sh` before and after a change. New behaviour
comes with a check, and the check is seen to fail once with the bug it guards
against (see [docs/VERIFY.md](docs/VERIFY.md)).

## Comments

Comment what a piece of code does where that is not obvious from the code:
summaries of modules and functions, units and coordinate frames, what data
values mean. Keep them short, and keep history and debate in pull requests,
not in the code.
