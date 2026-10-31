#include "Profile.h"

#include "hal/Platform.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Core::Profile {

namespace {

std::string Path() {
    const std::string base = HAL::Platform::PrefPath("Profile");
    return base.empty() ? "profile.txt" : base + "profile.txt";
}

} // namespace

Options Load() {
    Options options;
    std::ifstream in(Path());
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        std::string rest;
        std::getline(row, rest);
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        const int value = std::atoi(rest.c_str());
        if (key == "name") options.name = rest;
        else if (key == "screen_width") options.screen_width = value;
        else if (key == "screen_height") options.screen_height = value;
        else if (key == "music_volume") options.music_volume = value;
        else if (key == "sound_volume") options.sound_volume = value;
        else if (key == "scroll_speed") options.scroll_speed = value;
        else if (key == "game_speed") options.game_speed = value;
        else if (key == "two_button_mouse") options.two_button_mouse = value != 0;
        else if (key == "one_click_garrison") options.one_click_garrison = value != 0;
        else if (key == "audio_taunts") options.audio_taunts = value != 0;
        else if (key == "graphics_detail") options.graphics_detail = value;
        else if (key == "widescreen") options.widescreen = value != 0;
        else if (key == "no_selection_limit") options.no_selection_limit = value != 0;
        else if (key == "top_bar_size") options.top_bar_size = value;
        else if (key == "panel_size") options.panel_size = value;
        else if (key == "text_size") options.text_size = value;
        else if (key == "connection") options.connection = value;
        else if (key == "recent_game" && options.recent_games.size() < kRecentGames) {
            // The address, then the host's name.
            const size_t space = rest.find(' ');
            if (space != std::string::npos) {
                options.recent_games.push_back({rest.substr(space + 1), rest.substr(0, space)});
            }
        }
    }
    for (int* size : {&options.top_bar_size, &options.panel_size, &options.text_size}) {
        *size = std::clamp(*size, kInterfaceSizeLeast, kInterfaceSizeMost);
    }
    // No name yet: the machine's user name.
    if (options.name.empty()) {
        if (const char* user = std::getenv("USER")) options.name = user;
        else if (const char* user_name = std::getenv("USERNAME")) options.name = user_name;
    }
    // Only the three screen sizes.
    if (!((options.screen_width == 800 && options.screen_height == 600) ||
          (options.screen_width == 1024 && options.screen_height == 768) ||
          (options.screen_width == 1280 && options.screen_height == 1024))) {
        options.screen_width = 1024;
        options.screen_height = 768;
    }
    return options;
}

void Save(const Options& options) {
    std::ofstream out(Path());
    out << "name " << options.name << "\n"
        << "screen_width " << options.screen_width << "\n"
        << "screen_height " << options.screen_height << "\n"
        << "music_volume " << options.music_volume << "\n"
        << "sound_volume " << options.sound_volume << "\n"
        << "scroll_speed " << options.scroll_speed << "\n"
        << "game_speed " << options.game_speed << "\n"
        << "two_button_mouse " << (options.two_button_mouse ? 1 : 0) << "\n"
        << "one_click_garrison " << (options.one_click_garrison ? 1 : 0) << "\n"
        << "audio_taunts " << (options.audio_taunts ? 1 : 0) << "\n"
        << "graphics_detail " << options.graphics_detail << "\n"
        << "widescreen " << (options.widescreen ? 1 : 0) << "\n"
        << "no_selection_limit " << (options.no_selection_limit ? 1 : 0) << "\n"
        << "top_bar_size " << options.top_bar_size << "\n"
        << "panel_size " << options.panel_size << "\n"
        << "text_size " << options.text_size << "\n"
        << "connection " << options.connection << "\n";
    for (const RecentGame& game : options.recent_games) {
        out << "recent_game " << game.address << " " << game.name << "\n";
    }
}

Options& Current() {
    static Options options = Load();
    return options;
}

} // namespace Core::Profile
