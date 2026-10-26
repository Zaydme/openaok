#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

// A screen's look, as its .sin file in interfac.drs describes it:
// the background art for each screen size, the palette it is drawn in, how
// much a button darkens what is behind it, the button art, the six bevel
// colours and the text colours.
namespace Screens {

struct ScreenStyle {
    uint32_t backgrounds[3] = {0, 0, 0};   // 800x600, 1024x768, 1280x1024; 0 for none
    uint32_t palette = 50500;
    uint32_t cursors = 51000;
    int shade_percent = 50;                // how much darker a button's inside is
    uint32_t button_art = 0;               // the control textures, or 0
    uint8_t background_colour = 0;         // palette index under the art
    // Where the art goes: 0 at the top left, 1 centred, 2 tiled from the top
    // left (the same as 0 on a screen the size of its art).
    int background_position = 2;
    // The bevel, as palette indexes: the lit side outermost first, then the
    // shaded side innermost first, as the file lists them.
    uint8_t bevel[6] = {0, 0, 0, 0, 0, 0};
    SDL_Color text = {217, 208, 176, 255};
    SDL_Color text_shadow = {0, 0, 0, 255};
    SDL_Color focus = {202, 207, 1, 255};
    SDL_Color focus_shadow = {0, 0, 0, 255};
    SDL_Color state = {255, 255, 255, 255};
    SDL_Color state_shadow = {0, 0, 0, 255};
};

// Reads the .sin file with this id from the interface archive. A missing or
// unreadable file gives the defaults.
ScreenStyle LoadStyle(uint32_t sin_id);

// The .sin files the screens use, by id.
namespace Sin {
constexpr uint32_t kMainMenu = 50081;        // the town, with its signs
constexpr uint32_t kMainDialog = 50083;      // a message box over the town
constexpr uint32_t kPlayerSelect = 50080;
constexpr uint32_t kParchment = 50051;       // the game setup screens
constexpr uint32_t kParchment2 = 50052;
constexpr uint32_t kParchment3 = 50053;
constexpr uint32_t kParchment4 = 50054;
constexpr uint32_t kParchmentPlain = 50084;  // scr2p
constexpr uint32_t kParchmentDialog = 50086; // maindlg2, over a parchment
constexpr uint32_t kScoreDialog = 50013;     // the timeline
constexpr uint32_t kDiplomacy = 50014;
constexpr uint32_t kGameMenu = 50015;
constexpr uint32_t kObjectives = 50016;
constexpr uint32_t kChat = 50017;
constexpr uint32_t kGameDialog = 50018;      // options, save, load in the game
constexpr uint32_t kChat2 = 50019;
constexpr uint32_t kAchievements = 50061;
constexpr uint32_t kCredits = 50059;
constexpr uint32_t kHistory = 50062;
constexpr uint32_t kLoading = 50060;         // scr9b, the loading screen
constexpr uint32_t kScenario = 50057;        // the scenario's instructions
constexpr uint32_t kEditor = 50055;          // scr5, the scenario editor
constexpr uint32_t kCampaigns = 50267;
constexpr uint32_t kCampaignJoan = 50251;
constexpr uint32_t kCampaignSaladin = 50252;
constexpr uint32_t kCampaignGenghis = 50253;
constexpr uint32_t kCampaignBarbarossa = 50254;
constexpr uint32_t kCampaignWallace = 50258;
} // namespace Sin

} // namespace Screens
