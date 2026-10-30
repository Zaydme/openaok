# Roadmap

The 1999 game plays end to end: random maps, campaigns, scenarios, the scenario
editor, saved and recorded games, multiplayer and computer players, on desktop
and Android. What remains is completing the last screens and features, the
computer players' finer play, and polish.

## Done

- **Engine**: deterministic fixed-point simulation, lockstep multiplayer, the
  headless self-check.
- **Data**: everything read from the 1999 CD -- rules, art, sounds, music,
  movies, text, map and AI scripts -- prepared at build time.
- **World**: random maps from the original scripts, terrain blending,
  elevation, cliffs, water, fog of war.
- **Rules**: economy, production and research, combat, garrisons, monks and
  relics, siege, walls and gates, the navy, trade, tribute, diplomacy, victory
  conditions, Regicide and Death Match, cheat codes.
- **Computer players**: the original AI scripts, run by an interpreter, at
  every difficulty, up to seven at a table.
- **Interface**: the menus, the battlefield interface, the tech tree, the
  Achievements screens, the start-up movies, campaigns with their narration and
  pictures, the scenario editor.
- **Android**: touch controls, larger interface options, widescreen.
- **Nintendo Switch**: homebrew, with the touch controls and the controller
  played as a mouse and the game's hotkeys.

## Next

1. **Interface**
   - The multiplayer menu screens: hosting, joining and the lobby with chat and
     ready checks. Network games start from the command line for now.
   - The Disband Formation button.
2. **Computer players**
   - Walls and gates laid by the scripts (`build-wall`, `build-gate`).
   - Exploration and defence groups as the scripts' strategic numbers ask.
   - Relics, and the difficulty parameters (dodging missiles, reaction
     distances), which are read but not used.
   - Their economy on water maps: more wood in the Feudal Age, more warships
     and transports.
3. **Campaigns and the editor**
   - Custom campaigns' own maps.
   - Editor: Map Copy, garrisons on the Units tab, the AI script list, the
     Campaign Editor.
4. **World**
   - The grass the original paints under cliffs.
   - Spreading crowds of units more evenly.
5. **Performance**: eight players on a giant map, late game, at a steady
   60 Hz, measured by a stress scenario in the self-check.
6. **Platforms**: a tested Linux build, a Windows build, and on Android the
   on-screen keyboard keeping the text field it types into in view.
7. **The Conquerors**: the 2000 expansion, played from the player's own copy
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
  hardest reach the Feudal Age around minute 19 to 21.
- **57 graphics are missing** from the 1999 data; the game draws without them.
- **Builds are tested on macOS and Android.** Linux follows the same steps but
  is not yet tested; Windows is not supported yet.
- **On the Switch, network games** cannot be started: they start from the
  command line.
