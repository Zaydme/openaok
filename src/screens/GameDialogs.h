#pragma once
#include <SDL2/SDL.h>
#include <string>

#include "ecs/Simulation.h"

// The screens over a game in progress: the game menu (F10), the in-game
// options, objectives, diplomacy, chat, the technology tree, the pause and the
// end of the game. Each on its own art from interfac.drs, styled by its .sin.
namespace GameDialogs {

enum class Dialog {
    None,
    Menu,          // F10
    Options,
    Objectives,
    Diplomacy,
    Chat,
    TechTree,
    Achievements,  // the game's statistics, at its end or from the menu
    Confirm,       // a yes-or-no question over the menu
    SaveGame,      // a name to save the game under (F12, or the menu's Save)
    ChatLine,      // a line of chat typed over the game (Enter), to everyone
};

// What a dialog asks of the game.
// Ended: the end-of-game message has been clicked away -- what comes
// between it and the achievements (a campaign's slides, a scenario's
// aftermath) is the application's, which then opens them.
enum class Outcome { None, QuitToMenu, QuitGame, Restart, Resign, Save, Load, Ended };

void Open(Dialog dialog);
Dialog Current();
// Whether the dialog open has a line being typed into: the chat line, the
// Chat dialog, the Save Game name.
bool WantsText();
// A new game: no dialog, nothing paused.
void Reset();
// Sets the Achievements screen's page (Achievements::Page).
void ShowAchievementsPage(int page);

// The game stands still: a dialog is open in a single-player game, or the
// player paused it (F3).
bool Paused();
void TogglePause();

// Input for the dialog showing; true when it took the event, which then
// reaches nothing behind it.
bool HandleEvent(const SDL_Event& event, ECS::Simulation& sim);
void Draw(SDL_Renderer* r, ECS::Simulation& sim, int w, int h);
Outcome TakeOutcome();
// The name the Save Game dialog was given, with Outcome::Save.
std::string SaveName();

// The game has ended for this machine's player: the message, then the
// achievements -- with "Play Again" in a game against the computer.
void GameOver(bool won, bool single_player = true);

} // namespace GameDialogs
