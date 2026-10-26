#pragma once
#include <SDL2/SDL.h>

#include "ecs/Simulation.h"

// The Achievements screen, "scr10" (sin 50061): the score pages and the
// timeline. Positions are on the 800x600 art; the .sin names one background
// for all three screen sizes, centred, the rest filled with its palette colour.
//
// The screen reads the simulation's ECS::Score sheets, and for the Timeline
// the players' histories (ECS::Timeline, drawn by AchievementsTimeline), and
// nothing else of the game's state but names, colours and teams.
namespace Achievements {

// Page numbers, in tab order.
enum Page { kScore = 1, kMilitary = 2, kEconomy = 3, kTechnology = 4, kSociety = 5, kTimeline = 6 };

// Why the screen was opened, which decides its buttons:
// from the game menu, a button back to the game; at the end of the game, one
// to the main menu (or the campaign's menu), and in a game against the
// computer "Play Again" beside it.
struct Opening {
    bool game_over = false;
    bool campaign = false;     // a campaign scenario: 9883, back to its menu
    bool single_player = true; // offers "Play Again"
};

enum class Exit { None, ReturnToGame, MainMenu, CampaignMenu, PlayAgain };

struct State {
    int page = kScore;
    Opening opening;
    bool pressed_exit = false;   // the mouse went down on a button
    bool pressed_again = false;
};

void Draw(SDL_Renderer* r, ECS::Simulation& sim, int w, int h, const State& s, int mx, int my,
          bool held);

// A mouse button pressed (`down`) or released at (x, y). A tab turns the
// page as it is pressed; a button acts as it is released.
Exit Mouse(State& s, int w, int h, int x, int y, bool down);

} // namespace Achievements
