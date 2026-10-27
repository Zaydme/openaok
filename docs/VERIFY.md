# Verifying a change

```sh
bash tools/check_determinism.sh
```

builds the game, runs `OpenAoK --verify 3000` headless and checks its output
line by line. It takes about thirty seconds and names whatever fails.

To run only some scenarios while working on them (from `build/`):

```sh
AOE_VERIFY_ONLY=water,fishing ./OpenAoK --verify 10
```

`AOE_AI_TRACE=1` prints the computer players' goals, gatherer shares, escrow and
buildings minute by minute in the `ai`, `pace` and `seven` games.

## What it checks

Each scenario builds its own world from the same fixed seed and prints one line
that the script reads.

| Line | Checks |
|---|---|
| `fixed point` | No `float`, `double` or `ToFloat()` in the simulation's sources |
| `determinism` | A golden hash of the world through 3000 ticks of a standard game |
| `movement` | Every unit given an order reaches it |
| `planner` | Standing spots spread along a building's sides |
| `gathering` | A villager's round trip to a resource and a drop site; farms |
| `notices` | The news the interface notifies the player of: a sheep claimed, a farm run out, a Wonder begun, production halted, a relic picked up |
| `building` | A crew raising a Town Center; demolition |
| `production` | Prices, refusals, births beside the building, the population cap, cancelling, rally points |
| `techs` | Civilisation bonuses and tech trees, ages, research, upgrades in place |
| `combat` | Damage, a duel, arrows, towers, stances, formations, hunting, splash damage |
| `vision` | Fog per player, explored ground, buildings remembered in the fog |
| `garrison` | Entering and leaving, refusals, garrison arrows, healing, the town bell |
| `animals`, `duties`, `monks` | Herding, fleeing, grazing; patrol, follow, guard; conversion, healing, relics |
| `maps` | Islands, continents, beaches, cliffs and fish across several map scripts |
| `script` | The computer players' rule language: reading, rule passes, every original script loading for every civilisation, difficulty and map |
| `ai`, `pace`, `seven` | Twenty minutes of the computer against itself; each difficulty's pace; seven computer players |
| `crossing`, `landing`, `water`, `fishing`, `navy` | Transport ships, passability on water maps, docks and fishing, warships |
| `net` | A host and a client in lockstep over loopback, and a desync caught |
| `victory`, `modes` | Conquest, Wonders, relics; Regicide, Death Match, starting options |
| `trade`, `tribute`, `market` | Trade routes and their pay; tribute and its fee; market prices |
| `diplomacy`, `chat`, `cheats` | Stances, teams and shared sight; chat and flares; the cheat codes |
| `repair`, `packing`, `converted` | Repairing; siege engines packing; converted units keeping their maker's rules |
| `walls`, `waypoints` | Walls, gates and locking; queued waypoints |
| `scenario`, `editor` | Every scenario on the CD read; triggers; saving and loading; the scenario editor's edits |

## The golden hash

The `determinism` line compares against the hash recorded in
`tools/check_determinism.sh` (`GOLDEN`). A change meant to alter the
simulation moves it: update `GOLDEN` after confirming the new hash is the same
on a second run. A hash that moves when nothing was meant to change is a bug.

## Adding a check

Make sure a new check can fail: introduce the bug it guards against, rebuild,
and confirm the verdict names it.
