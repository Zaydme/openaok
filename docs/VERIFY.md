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
| `dropoff` | A villager sent to drop off: its whole load, part units included, and then it stands; only at its own finished building that takes it; the load banked on going into the Town Center, not a tower |
| `spies` | Spies' price for each enemy villager and the enemy sight it gives; none in Death Match or Regicide, where Treason marks the enemy Kings for ten seconds |
| `flag`, `capture` | A player's flag in a scenario seeing round itself and drawn; Gaia's villagers and buildings taken by the first player to see them, its animals and relic carts not |
| `foundations` | Foundations as the original lays them: no ground held, shown only to their side and nobody's target until begun; what placing runs into; overlapping foundations; footprint sight while going up; refunds on delete; the hill rules for uneven ground; a dock half ashore |
| `fog` | What a player keeps of what it does not see: no building drawn that it never saw; a picture of one gone out of sight, its alone, kept after the building is destroyed until its ground is seen; a picture of a mine used up unseen; ground laid unseen remembered as it was |
| `notices` | The news the interface notifies the player of: a sheep claimed, a farm run out, a Wonder begun and seen by every player, production halted, a building site found taken, a relic picked up |
| `building` | A crew raising a Town Center; demolition |
| `production` | Prices, refusals, births beside the building, the population cap, cancelling, rally points |
| `techs` | Civilisation bonuses and tech trees, ages, research, upgrades in place |
| `combat` | Damage, reach between the dat's boxes, a duel, arrows, towers, stances, formations, hunting, splash damage |
| `wolves` | A computer player's scout and two wolves in its town: every swing it begins lands |
| `vision` | Fog per player, explored ground, buildings remembered in the fog |
| `garrison` | Entering and leaving, refusals, garrison arrows, healing, the town bell |
| `animals`, `duties`, `monks` | Herding, fleeing, grazing; patrol, follow, guard; conversion, facing whom they convert, healing, relics |
| `maps` | Islands, continents, beaches, cliffs and fish across several map scripts |
| `script` | The computer players' rule language: reading, rule passes, every original script loading for every civilisation, difficulty and map, strategic numbers scaled for the difficulty and kept in bounds, civilian explorers, retasking, mining camps and forward builders as the game reckons them, an ally turned on after too many blows, explore groups of one soldier, the scout sent on as sn-consecutive-idle-unit-limit says, Monks fetching relics |
| `ai`, `pace`, `seven` | Twenty minutes of the computer against itself; each difficulty's pace; seven computer players |
| `crossing`, `landing`, `water`, `fishing`, `navy` | Transport ships, passability on water maps, docks and fishing, warships |
| `boats` | A computer player's boats: fishing ships keep near a Dock as the rules ask, a spare warship explores the water |
| `attackgroups` | A computer player's attack groups: made, filled, gathered and sent on their own once the enemy is known, as the game does; blows answered by a share of the idle soldiers near, none for explorers |
| `net` | A host and a client in lockstep over loopback, and a desync caught |
| `lobby` | The multiplayer lobby over loopback: the game found, joined, rows, ready checks and chat, a refused start, the game started from the table |
| `victory`, `modes` | Conquest, Wonders, relics; Regicide, Death Match, starting options |
| `trade`, `tribute`, `market` | Trade routes and their pay; tribute and its fee; market prices |
| `diplomacy`, `chat`, `cheats` | Stances, teams and shared sight; chat and flares; the cheat codes |
| `repair`, `packing`, `converted` | Repairing; siege engines packing, their icons from the units' sheet, placed as units from a scenario; converted units keeping their maker's rules |
| `formations` | Locked formations: kept wherever one of the group is sent, lit on the panel, Disband Formation, another formation for some, a member lost |
| `walls`, `waypoints` | Walls, gates going up as their site units, and locking; queued waypoints |
| `scenario`, `editor` | Every scenario on the CD read; triggers; saving and loading; the scenario editor's edits, Map Copy, garrisons and personalities; campaign files written |

## The golden hash

The `determinism` line compares against the hash recorded in
`tools/check_determinism.sh` (`GOLDEN`). A change meant to alter the
simulation moves it: update `GOLDEN` after confirming the new hash is the same
on a second run. A hash that moves when nothing was meant to change is a bug.

## Adding a check

Make sure a new check can fail: introduce the bug it guards against, rebuild,
and confirm the verdict names it.
