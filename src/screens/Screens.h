#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

#include "core/GameSetup.h"
#include "net/Lockstep.h"
#include "scenario/Scenario.h"

// The screens before and around a game: the main menu with its single
// player menu, the game setup screens, options, the multiplayer screens,
// history, credits and the loading screen; the campaigns, a campaign's map,
// the custom scenarios and a scenario's instructions (screens/Campaigns.cpp).
// Each is drawn on its own art from interfac.drs in its own palette, with
// the controls its .sin file styles, at 800x600.
//
// The application asks what the screens want done (start a game, quit) and
// does it; the screens never touch the simulation.
namespace Screens {

enum class Id {
    None,          // the game is running
    MainMenu,
    GameSetup,     // a single-player Random Map, Regicide or Death Match game
    Options,
    Multiplayer,   // the wait while a network game is joined
    MultiplayerSetup,  // a network game's lobby
    History,
    Credits,
    Loading,
    Campaigns,     // the book of the campaigns
    CampaignMap,   // one campaign's map and its scenarios
    ScenarioSelect,  // Custom Scenario: the scenario files to choose from
    Mission,       // a scenario's instructions before its game
    Aftermath,     // a scenario's victory or loss text after its game
    CustomCampaigns,  // Custom Campaign: a campaign of the player's and its scenario
    SavedGames,
};

// The screen size every menu is drawn at.
constexpr int kMenuWidth = 800;
constexpr int kMenuHeight = 600;

// What the screens ask the application to do.
struct Request {
    enum class Kind {
        None,
        Quit,
        StartGame,     // `setup` is the game to start
        NetworkGame,   // the lobby's game has started: TakeNetworkSession
        LoadGame,      // the saved game at `path`
        PlayRecording, // the recorded game at `path`
        // The scenario at `path` -- a campaign's `entry`th when `entry` is not
        // -1 -- at `setup.difficulty`.
        StartScenario,
        Begin,         // the scenario's instructions or aftermath read: go on
        CreateScenario,  // the scenario editor, on a new scenario
        EditScenario,    // the scenario editor, choosing one to open
    };
    Kind kind = Kind::None;
    Core::GameSetup setup;
    std::string address;
    std::string path;
    int entry = -1;
    int port = 0;
};

// Loads the screens' styles. After the interface archive and the fonts.
void Init();

void Open(Id screen);
Id Current();

// One input event for the screen showing. Mouse positions are the screen's
// pixels.
void HandleEvent(const SDL_Event& event);
// Draws the screen showing.
void Draw(SDL_Renderer* r);
// What the screens want done, once.
Request TakeRequest();
// The screens' network each frame: games looked for, a lobby's messages.
void Update();
// The network game a lobby started, for the application to play.
Net::Session TakeNetworkSession();
// The saved games, to host one again as a network game.
void OpenRestoreList();
// The main menu's Multiplayer menu, open on the plank.
void OpenMultiplayerMenu();

// The loading screen's progress, 0 to 100, while a game is made.
void SetLoadingProgress(int percent);

// Where saved games and recorded games are kept, ending in a separator.
std::string SavedGamesDir();
// Where the scenario editor saves scenarios: beside the saved games.
std::string ScenariosDir();
// Where the campaign editor saves campaigns, beside them.
std::string CampaignsDir();
// The files in `dir` with one of `exts`, sorted.
std::vector<std::string> FilesIn(const std::string& dir, const std::vector<std::string>& exts);

// The saved games list, as the main menu's Saved Game button opens it.
void OpenSavedGames();

// A campaign's map: `number` is its file's (cam1 Joan of Arc ... cam8
// William Wallace). From the main menu's Learn to Play its way back is the
// main menu; from the Campaigns screen, the Campaigns screen.
void OpenCampaignMap(int number, bool from_main_menu);

// The scenario's instructions, before its game: the Mission Dialog. The
// file must outlive the screen.
void OpenMission(const Scenario::File& file);

// A scenario's end: its victory or loss text (the Aftermath Screen). False,
// and nothing to show, when the file has none.
bool OpenAftermath(const Scenario::File& file, bool won);

// After a scenario's game: back to the screen it was started from -- the
// campaign's map, or the main menu.
void ReturnFromScenario();

} // namespace Screens
