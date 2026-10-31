# Roadmap

The 1999 game plays end to end: random maps, campaigns, scenarios, the scenario
editor, saved and recorded games, multiplayer and computer players, on Windows,
macOS, Linux, Android and the Nintendo Switch. What remains is the last of the
editor and campaigns, the computer players' finer play, and polish.

## Done

- **Engine**: deterministic fixed-point simulation, lockstep multiplayer, the
  headless self-check.
- **Data**: everything read from the 1999 CD -- rules, art, sounds, music,
  movies, text, map and AI scripts -- prepared at build time.
- **World**: random maps from the original scripts, terrain blending,
  elevation, cliffs, water, fog of war; units sent to one point standing round
  it, each in a place of its own.
- **Rules**: economy, production and research, combat, garrisons, monks and
  relics, siege, walls and gates, the navy, trade, tribute, diplomacy, victory
  conditions, Regicide and Death Match, cheat codes.
- **Computer players**: the original AI scripts, run by an interpreter, at
  every difficulty, up to seven at a table.
- **Interface**: the menus, the battlefield interface, the tech tree, the
  Achievements screens, the start-up movies, campaigns with their narration and
  pictures, Custom Campaign; the scenario editor with Map Copy, garrisons and
  the personalities, and the Campaign Editor; the multiplayer menu and lobby, with games
  found on the local network or joined by address, chat and ready checks;
  locked formations and Disband Formation. Beyond the original: zoom, the
  minimap's fog of war, no selection limit, populations up to 1000 and the
  rest listed in [docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md).
- **Performance**: the stress scenario in the self-check, eight computer
  players on a giant map in the Imperial Age with nearly three thousand units
  and buildings, runs at about a third of a millisecond a tick on a desktop.
- **Linux**: built and checked on Debian 13 (GCC 14).
- **Windows**: 64-bit, built with MinGW-w64 (windows/build.sh) and released
  as a folder to unzip; the data prepared from the CD on first start.
- **Android**: touch controls, larger interface options, widescreen.
- **Nintendo Switch**: homebrew, with the touch controls and the controller
  played as a mouse and the game's hotkeys.

## Next

1. **Computer players**
   - The priority of their units' orders (sn-zero-priority-distance), which
     needs order priorities in the units' own behaviour.
   - Defend groups and what they guard (the defend priorities), which only
     players' own scripts ask for: the game's scripts leave
     sn-number-defend-groups at 0.
   - Their economy on water maps: more wood in the Feudal Age, and more
     warships.
2. **Performance on phones and the Switch**: fewer draw calls for the
   world's sprites, which each graphic's own texture keeps apart, and the
   chase routes of large fights.
3. **The Conquerors**: the 2000 expansion, played from the player's own copy
   when its files are in `original_game_1999/`, with the 1999 game unchanged.
   - Its data: the dat, the expansion's archives and strings, its scenarios
     and campaigns, its random map and AI scripts.
   - The rules it changed, each matched to the expansion's own game: farms
     reseeded from the Mill, villagers gathering after building a drop site,
     King of the Hill, Wonder Race and Defend the Wonder, its interface.

## Known issues

- **In-game music.** The original plays its in-game music from the CD's audio
  tracks, which are not files and are not copied with the CD's files. Rip the
  tracks to `original_game_1999/GAME/SOUND/STREAM/MUSIC1.MP3`, `MUSIC2.MP3`, ...
  to hear them; menu, civilisation and campaign music play from the files
  as they are.
- **Computer players are slow to advance.** On the standard test map the
  hardest reach the Feudal Age around minute 17 to 20.
- **57 graphics are missing** from the 1999 data; the game draws without them.
- **On the Switch, network games** from the multiplayer menu are not yet
  tested.
