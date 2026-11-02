# Command line

OpenAoK starts at its main menu. From the command line it can also start
straight into a game, a scenario, a saved game or a network game, with the
lobby's settings given as options. On a desktop run it from `build/`:

```sh
cd build
./OpenAoK --game --map Arabia --size small --difficulty hard
```

## A game

| Option | |
|---|---|
| `--game` | Straight into a random map game against the computer |
| `--map NAME` | The map: a random map script by name (Arabia, Archipelago, Baltic, BlackForest, Coastal, Continental, CraterLake, Fortress, GoldRush, Highland, Islands, Mediterranean, Migration, Nomad, Rivers, TeamIslands) |
| `--size S` | tiny, small, medium, normal, large or giant |
| `--seed N` | The map's seed: the same seed makes the same map |
| `--seats N` | An offline game of this many seats, the computer in the rest |
| `--difficulty D` | hardest, hard, moderate, easy or easiest |
| `--resources R` | standard, low, medium or high starting resources |
| `--age A` | dark, feudal, castle or imperial starting age |
| `--reveal V` | normal, explored or all |
| `--population N` | The population limit: the largest of the lobby's choices (25 to 1000) not over N |
| `--regicide`, `--deathmatch` | A Regicide game, or a Death Match |
| `--cheats` | Allow cheats |
| `--ai-only` | The computer plays every seat, yours too: to watch |
| `--no-ai` | Nobody plays the other seats |

## Scenarios, saves and recordings

| Option | |
|---|---|
| `--scenario FILE` | Plays a scenario |
| `--load FILE` | Loads a saved game |
| `--record` | Records the game, as the lobby's Record Game does |
| `--play FILE` | Plays a recorded game back |
| `--editor [FILE]` | Opens the scenario editor, with a scenario if one is named |

## Network games

| Option | |
|---|---|
| `--host PORT --players N` | Opens a network game on this port, this machine at seat 1, started once N people are in |
| `--join HOST:PORT` | Joins the network game at that address; the seat and the game come from the host |

These skip the lobby; the Multiplayer menu is described in
[Multiplayer](/guide/multiplayer).

## The window

| Option | |
|---|---|
| `--windowed`, `--fullscreen` | In a window, or full screen |
| `--widescreen` | The frame as wide as the screen |
| `--resolution 800x600`, `1024x768`, `1280x1024` | The screen size the frame is laid out at |
| `--zoom Z` | The first frame at this zoom |
| `--mute` | No sound device at all |
| `--vsync`, `--no-vsync` | Wait for the display's refresh, or not |
| `--fps` | The frame rate under the top bar |

## For testing

Options like `--screenshot PATH --frames N` (render that many frames, write the
image and exit), `--select NAME`, `--spawn NAME`, `--place NAME`, `--look X Y`,
`--advance SECONDS` and `--verify TICKS` exist to test the engine; they are
described in [Building from source](/development/) and
[Self-checks](/development/verification).
