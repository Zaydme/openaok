#pragma once
#include <string>
#include <vector>

// The player's profile, kept between runs: the name the player plays under
// and the Options screen's choices. A plain text file of `key value` lines in
// the user's own preferences folder.
namespace Core::Profile {

// A game joined by address, for the Show Games dialog's Recent Games.
struct RecentGame {
    std::string name;            // its host's name
    std::string address;
};
constexpr size_t kRecentGames = 10;

struct Options {
    std::string name;            // empty until the player picks one
    int screen_width = 1024;     // one of the three screen sizes
    int screen_height = 768;
    int music_volume = 7;        // 0 (off) to 10 (high)
    int sound_volume = 8;
    int scroll_speed = 5;        // 0 (slow) to 10 (fast)
    int game_speed = 1;          // 0 slow, 1 normal, 2 fast
    bool two_button_mouse = true;
    bool one_click_garrison = false;
    bool audio_taunts = true;
    int graphics_detail = 2;     // 0 low, 1 medium, 2 high
    // The game's frame as wide as the screen, the interface reaching its
    // edges (rendering/HudLayout.h). Off, the frame is 4:3.
#if defined(__ANDROID__) || defined(__SWITCH__)
    bool widescreen = true;      // a phone and the Switch are far wider than 4:3
#else
    bool widescreen = false;
#endif
    // How big the interface is drawn, in percent of its base size, 100 to
    // 200: the top bar, the bottom panel
    // (with the map and the help), and the text over the world (chat, the
    // game's messages, scores and timers). A bar grows only as far as the
    // frame has room for it (rendering/UIRenderer's HudSpace).
#if defined(__ANDROID__) || defined(__SWITCH__)
    int top_bar_size = 150;      // a handheld's screen is small and far off
    int panel_size = 125;
    int text_size = 150;
#else
    int top_bar_size = 100;
    int panel_size = 100;
    int text_size = 100;
#endif
    // The multiplayer menu's connection type last chosen (0 TCP/IP, 1 the
    // local network), and the games last joined by address, newest first.
    int connection = 1;
    std::vector<RecentGame> recent_games;
};

// The sizes the Options screens offer, in percent.
constexpr int kInterfaceSizeLeast = 100;
constexpr int kInterfaceSizeMost = 200;

// Reads the profile; a missing file leaves the defaults.
Options Load();
void Save(const Options& options);

// The profile in use this run: read once, changed by the Options screens.
Options& Current();

// The game speeds Slow, Normal and Fast, in hundredths.
constexpr int kGameSpeedPercent[3] = {100, 150, 200};

} // namespace Core::Profile
