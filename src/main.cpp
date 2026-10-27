#include <iostream>
#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include "ecs/components/Transform.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Health.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Worker.h"
#include "core/Command.h"
#include "core/CommandQueue.h"
#include "verify/AIGame.h"
#include "verify/Harness.h"

#include <SDL2/SDL.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>
#endif
#include "hal/Audio.h"
#include "hal/Pad.h"
#include "hal/Platform.h"
#include "hal/TextInput.h"
#include "hal/Touch.h"
#include "hal/Window.h"
#include "ecs/Simulation.h"
#include "rendering/Renderer.h"
#include "rendering/EntityRenderer.h"
#include "rendering/Alerts.h"
#include "rendering/TerrainRenderer.h"
#include "rendering/Messages.h"
#include "save/Recording.h"
#include "save/Save.h"
#include "scenario/Campaign.h"
#include "scenario/Scenario.h"
#include "rendering/UIRenderer.h"
#include "rendering/Text.h"
#include "core/ConfigManager.h"
#include "core/GameSetup.h"
#include "core/Hotkeys.h"
#include "core/InputManager.h"
#include "core/Media.h"
#include "core/Profile.h"
#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/Players.h"
#include "ecs/Units.h"
#include "core/IsometricMath.h"
#include "hal/GraphicCache.h"
#include "audio/Music.h"
#include "audio/SoundSystem.h"
#include "ai/Doctrine.h"
#include "net/Lockstep.h"
#include "ecs/Triggers.h"
#include "ecs/Victory.h"
#include "ecs/Buildings.h"
#include "ecs/Walls.h"
#include "editor/Editor.h"
#include "screens/GameDialogs.h"
#include "screens/Movies.h"
#include "screens/Screens.h"
#include "screens/Slides.h"

#ifdef _WIN32
// Force High-Performance GPU on Windows (NVIDIA & AMD)
extern "C" {
    __declspec(dllexport) uint32_t NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

// Everything the command line can ask for: the lobby's choices for a game
// started straight from it, and the development tools.
struct Launch {
    // --screenshot <path> renders a few frames, writes the frame to a BMP and
    // exits.
    std::string screenshot_path;
    std::string map_image_path;
    std::string select_name;
    std::string select_all_name;
    // --menu NAME: open on one of the menu screens instead of a game.
    std::string menu;
    // --no-movies: straight to the menus (NOSTARTUP).
    bool no_movies = false;
    // --movie NAME[:FRAME]: that movie alone instead of the three; with
    // --screenshot, an image of its picture FRAME as shown.
    std::string movie;
    int movie_frame = 0;
    int train_count = 0;
    int build_menu = 0;
    bool rally = false;
    bool show_grid = false;
    bool mute = false;
    bool no_ai = false;
    int host_port = 0;
    int host_players = 2;
    std::string join_address;
    bool ai_only = false;
    int look_x = -1;
    int look_y = -1;
    float zoom = 0.0f;
    int hurt_percent = 0;
    int garrison_count = 0;
    int battle_size = 0;
    bool walls = false;
    std::string place_name;
    int advance_seconds = 0;
    // --achievements: the Achievements screen open on this page (1-6) once the
    // simulation has advanced; with --achievements-end, as the game's end
    // opens it.
    int achievements_page = 0;
    bool achievements_end = false;
    int screenshot_after_frames = 30;
    // --clicks "x,y;x,y;...": left clicks at these points of the frame, one
    // every few frames from the start; --screenshot waits for the last of
    // them. An entry "key:ctrl+1" presses a key instead (modifiers ctrl+,
    // alt+, shift+, then an SDL key name: "1", "H", "Home", "Keypad +"),
    // "text:words" types, "right:x,y" clicks the right button there,
    // "move:x,y" moves the pointer there and holds it, and "down:x,y"
    // presses the left button there without letting go.
    struct Step {
        SDL_Point at{0, 0};
        SDL_Keycode key = SDLK_UNKNOWN;
        Uint16 mod = KMOD_NONE;
        std::string text;   // "text:words" types them
        Uint8 button = SDL_BUTTON_LEFT;
        bool move = false;
        bool held = false;   // pressed and not let go
    };
    std::vector<Step> clicks;
    // --load PATH: open on a saved game. --play PATH: watch a recorded game.
    // --record: record this game (the lobby's Record Game).
    std::string load_path;
    std::string play_path;
    int seats = 2;
    bool report_frames = false;
    // --perf: once a second, what the frames cost, stage by stage.
    bool perf = false;
    // --scenario PATH: a game of a scenario (PATH:N, a campaign's Nth).
    std::string scenario_path;
    // --editor [PATH]: the scenario editor, on a new scenario or the one at
    // PATH; --editor-tab N shows its Nth tab (1 Map ... 10 Triggers) and
    // --look X Y its view's centre.
    bool editor = false;
    std::string editor_path;
    int editor_tab = 0;
    // --mission: with --scenario and --screenshot, the image is of the
    // scenario's instructions screen instead of the game.
    bool mission = false;
    // --slides NAME[:MS]: one show of campaign slides on its own ("c1s1_beg",
    // "c8s7_end", "intro"); with --screenshot, the image MS milliseconds in.
    std::string slides;
    int slides_at = 0;
    // --verify <ticks> runs the simulation headless for a fixed number of
    // ticks and prints a hash of the result, identical on every machine and
    // at every frame rate.
    int verify_ticks = 0;
    bool show_fps = false;
    // Straight into a game, without the menus: --game, or any flag that only
    // makes sense in one.
    bool direct = false;
    // --widescreen: this run's frame as wide as the screen, whatever the
    // profile says (Profile::Options::widescreen).
    bool widescreen = false;
    // --frame-width N: the game's frame N wide at its height, as a screen of
    // another shape (a phone's) would make it.
    int frame_width = 0;
    Core::GameSetup setup = Core::DefaultSetup();
    HAL::Window::Config window;
};

bool LoadData() {
    if (!Core::ConfigManager::Get().LoadExtractedData("../assets/extracted_data/units.json", "../assets/extracted_data/graphics.json")) {
        std::cerr << "[App] Failed to load unit definitions." << std::endl;
        return false;
    }
    // Companion shadow graphics for the sprites.
    Core::ConfigManager::Get().LoadShadowMap("../assets/extracted_data/shadows.json");
    // Multi-part building composition: a Town Center is a main unit plus annex
    // units placed at tile offsets. The dat-derived table loads first, then
    // corrections on top of it.
    Core::ConfigManager::Get().LoadAnnexes("../assets/extracted_data/annexes.dat.json");
    Core::ConfigManager::Get().LoadAnnexes("../assets/extracted_data/annexes.overrides.json");
    // Attack graphics and per-graphic frame timing.
    Core::ConfigManager::Get().LoadAnimations("../assets/extracted_data/animations.json");
    // Icons, command-panel slots, costs, stats, names and help text -- everything
    // the HUD shows about a unit.
    Core::ConfigManager::Get().LoadInterfaceData("../assets/extracted_data/interface.json");
    // Terrain art, plus the names the random map scripts use for it.
    Core::ConfigManager::Get().LoadTerrainData("../assets/extracted_data/terrain.json");
    // Building art, resolved by name per architecture set. The unit table's own
    // standing graphic names the wrong piece for several buildings.
    Core::ConfigManager::Get().LoadBuildingArt("../assets/extracted_data/buildings.json");
    // The rules the game is played by: every unit's numbers, the technologies
    // and their effects, and each civilisation's starting table.
    if (!Core::Rules::Load("../assets/extracted_data/rules.json")) {
        std::cerr << "[App] Failed to load the game rules." << std::endl;
        return false;
    }
    // The art and sound the rules refer to: composite graphics such as a
    // burning building's flames, and the sound table.
    Core::Media::Load("../assets/extracted_data/media.json");
    // The computer players' figures, from the game's AI files.
    AI::Doctrine::Load("../assets/extracted_data/ai.json");
    // The game's text, by string id.
    Core::Strings::Load("../assets/extracted_data/strings.json");
    Core::Hotkeys::Load("../assets/extracted_data/hotkeys.json");
    // The random map scripts and AI. Opened before the simulation, whose map
    // generation runs a script, and before the window, since --verify
    // generates a map without one.
    HAL::GraphicCache::Get().OpenGameData("../assets/original/Data/gamedata.drs");
    return true;
}

void ParseFlags(int argc, char* argv[], Launch& launch) {
    Core::GameSetup& setup = launch.setup;
    auto& win_config = launch.window;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--vsync") {
            win_config.vsync = true;
        } else if (arg == "--no-vsync") {
            // Benchmarking: frames as fast as the frame cap allows.
            win_config.vsync = false;
        } else if (arg == "--resolution" && i + 1 < argc) {
            // The three screen sizes: 800x600, 1024x768 or 1280x1024.
            const std::string size = argv[++i];
            for (const HAL::ScreenSize& option : HAL::kScreenSizes) {
                if (size == std::to_string(option.width) + "x" + std::to_string(option.height) ||
                    size == std::to_string(option.width)) {
                    win_config.game_width = option.width;
                    win_config.game_height = option.height;
                }
            }
        } else if (arg == "--fps") {
            // The frame rate under the top bar.
            launch.show_fps = true;
        } else if (arg == "--name" && i + 1 < argc) {
            // What the interface calls the player at this machine.
            Core::Profile::Current().name = argv[++i];
        } else if (arg == "--widescreen") {
            launch.widescreen = true;
        } else if (arg == "--frame-width" && i + 1 < argc) {
            launch.frame_width = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--game") {
            // Straight into a game from the command line's lobby.
            launch.direct = true;
        } else if (arg == "--no-movies" || arg == "--nostartup") {
            // No movies at start-up (NOSTARTUP).
            launch.no_movies = true;
        } else if (arg == "--movie" && i + 1 < argc) {
            // One movie -- mslogo, eslogo or age2 -- in place of the three;
            // NAME:FRAME with --screenshot writes that picture of it.
            launch.movie = argv[++i];
            const size_t colon = launch.movie.find(':');
            if (colon != std::string::npos) {
                launch.movie_frame = std::atoi(launch.movie.c_str() + colon + 1);
                launch.movie.resize(colon);
            }
        } else if (arg == "--menu" && i + 1 < argc) {
            // A menu screen to show: main, single, setup, options, history,
            // multiplayer, scenarios, loading, campaigns, learn (William
            // Wallace's map, as Learn to Play opens it), campaign1 ..
            // campaign8 (a campaign's map, as the Campaigns screen opens it).
            launch.menu = argv[++i];
        } else if (arg == "--mission") {
            launch.mission = true;
        } else if (arg == "--slides" && i + 1 < argc) {
            launch.slides = argv[++i];
            const size_t colon = launch.slides.find(':');
            if (colon != std::string::npos) {
                launch.slides_at = std::atoi(launch.slides.c_str() + colon + 1);
                launch.slides.resize(colon);
            }
        } else if (arg == "--clicks" && i + 1 < argc) {
            const std::string list = argv[++i];
            size_t start = 0;
            while (start < list.size()) {
                size_t end = list.find(';', start);
                if (end == std::string::npos) end = list.size();
                std::string point = list.substr(start, end - start);
                const size_t comma = point.find(',');
                if (point.rfind("text:", 0) == 0) {
                    Launch::Step step;
                    step.text = point.substr(5);
                    launch.clicks.push_back(step);
                } else if (point.rfind("key:", 0) == 0) {
                    Launch::Step step;
                    point.erase(0, 4);
                    for (const auto& [prefix, mod] :
                         {std::pair<const char*, Uint16>{"ctrl+", KMOD_LCTRL},
                          {"alt+", KMOD_LALT}, {"shift+", KMOD_LSHIFT}}) {
                        if (point.rfind(prefix, 0) == 0) {
                            step.mod = static_cast<Uint16>(step.mod | mod);
                            point.erase(0, std::strlen(prefix));
                        }
                    }
                    step.key = SDL_GetKeyFromName(point.c_str());
                    if (step.key != SDLK_UNKNOWN) launch.clicks.push_back(step);
                } else if (comma != std::string::npos) {
                    Launch::Step step;
                    size_t from = 0;
                    if (point.rfind("right:", 0) == 0) {
                        step.button = SDL_BUTTON_RIGHT;
                        from = 6;
                    } else if (point.rfind("move:", 0) == 0) {
                        step.move = true;
                        from = 5;
                    } else if (point.rfind("down:", 0) == 0) {
                        step.held = true;
                        from = 5;
                    }
                    step.at = SDL_Point{std::atoi(point.c_str() + from),
                                        std::atoi(point.c_str() + comma + 1)};
                    launch.clicks.push_back(step);
                }
                start = end + 1;
            }
        } else if (arg == "--load" && i + 1 < argc) {
            launch.load_path = argv[++i];
            launch.direct = true;
        } else if (arg == "--play" && i + 1 < argc) {
            launch.play_path = argv[++i];
            launch.direct = true;
        } else if (arg == "--scenario" && i + 1 < argc) {
            launch.scenario_path = argv[++i];
            launch.direct = true;
        } else if (arg == "--editor") {
            launch.editor = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') launch.editor_path = argv[++i];
        } else if (arg == "--editor-tab" && i + 1 < argc) {
            launch.editor_tab = std::atoi(argv[++i]);
        } else if (arg == "--record") {
            launch.setup.record = true;
        } else if (arg == "--windowed") {
            win_config.fullscreen = false;
        } else if (arg == "--fullscreen") {
            win_config.fullscreen = true;
        } else if (arg == "--map-image" && i + 1 < argc) {
            // Writes the whole map, a few pixels a tile in each terrain's
            // minimap colour with the starts marked, and exits.
            launch.map_image_path = argv[++i];
            launch.direct = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            launch.screenshot_path = argv[++i];
        } else if (arg == "--select" && i + 1 < argc) {
            // With --screenshot: select the nearest entity of this dat unit
            // (FOAK, GOLDM, SHEEPG, VMBAS...) to the start and frame it.
            launch.select_name = argv[++i];
        } else if (arg == "--select-all" && i + 1 < argc) {
            // With --screenshot: select every unit of the player's of this
            // dat unit.
            launch.select_all_name = argv[++i];
        } else if (arg == "--train" && i + 1 < argc) {
            // With --select on a building: queue this many of the first unit
            // it trains.
            launch.train_count = std::atoi(argv[++i]);
        } else if (arg == "--hurt" && i + 1 < argc) {
            // With --select: take this percentage of its hit points away. A
            // test tool, not an order.
            launch.hurt_percent = std::atoi(argv[++i]);
        } else if (arg == "--garrison" && i + 1 < argc) {
            // With --select on a building: send this many of the player's
            // villagers into it, through the order a right-click gives.
            launch.garrison_count = std::atoi(argv[++i]);
        } else if (arg == "--battle" && i + 1 < argc) {
            // Two small armies, spearmen in front and archers behind, facing
            // each other a little way from the Town Center. A test tool.
            launch.battle_size = std::atoi(argv[++i]);
        } else if (arg == "--place" && i + 1 < argc) {
            // A finished building of this dat unit (MRKT, CSTL...) of the
            // player's beside the start, for --select to pick. A test tool.
            launch.place_name = argv[++i];
        } else if (arg == "--walls") {
            // Walls beside the start, finished: palisade runs every way, a
            // stone wall with a tower in it, and gates. A test tool.
            launch.walls = true;
        } else if (arg == "--look" && i + 2 < argc) {
            // Centre the first frame on this tile rather than the Town Center.
            launch.look_x = std::atoi(argv[++i]);
            launch.look_y = std::atoi(argv[++i]);
        } else if (arg == "--zoom" && i + 1 < argc) {
            // The first frame at this zoom (the mouse wheel's).
            launch.zoom = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--mute") {
            // No sound device at all.
            launch.mute = true;
        } else if (arg == "--show-grid") {
            // Outline every blocked tile as the simulation reads it.
            launch.show_grid = true;
        } else if (arg == "--rally") {
            // With --select on a building: set its rally point a few tiles
            // in front of it, through the same order a right-click gives.
            launch.rally = true;
        } else if (arg == "--build-menu" && i + 1 < argc) {
            // With --select on a villager: open its economic or military
            // build menu.
            const std::string page = argv[++i];
            launch.build_menu = page == "military" ? Core::ConfigManager::kInterfaceMilitary
                                                   : Core::ConfigManager::kInterfaceEconomic;
        } else if (arg == "--advance" && i + 1 < argc) {
            // Run the simulation this many seconds before the first frame.
            launch.advance_seconds = std::atoi(argv[++i]);
        } else if (arg == "--achievements" && i + 1 < argc) {
            launch.achievements_page = std::atoi(argv[++i]);
        } else if (arg == "--achievements-end") {
            launch.achievements_end = true;
        } else if (arg == "--verify" && i + 1 < argc) {
            launch.verify_ticks = std::atoi(argv[++i]);
        } else if (arg == "--resources" && i + 1 < argc) {
            // The lobby's starting resources: standard, low, medium or high.
            const std::string level = argv[++i];
            if (level == "low") setup.resources = Core::StartingResources::Low;
            else if (level == "medium") setup.resources = Core::StartingResources::Medium;
            else if (level == "high") setup.resources = Core::StartingResources::High;
            else setup.resources = Core::StartingResources::Standard;
        } else if (arg == "--reveal" && i + 1 < argc) {
            // The lobby's map visibility: normal, explored or all.
            const std::string reveal = argv[++i];
            setup.reveal = reveal == "explored" ? Core::Reveal::Explored
                         : reveal == "all" ? Core::Reveal::AllVisible
                                           : Core::Reveal::Normal;
        } else if (arg == "--map" && i + 1 < argc) {
            // The lobby's map: a random map script by name (Arabia,
            // Coastal, Islands, Rivers...).
            setup.map_script = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            // The lobby's map size: tiny, small, medium, normal, large, giant.
            const std::string size = argv[++i];
            for (const Core::MapSize& s : Core::kMapSizes) {
                if (size == s.name) setup.map_size = s.tiles;
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            setup.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--host" && i + 1 < argc) {
            // Opens a network game on this port, this machine at seat 1.
            launch.host_port = std::atoi(argv[++i]);
            launch.direct = true;
        } else if (arg == "--players" && i + 1 < argc) {
            // With --host: how many people play, the host included.
            launch.host_players = std::atoi(argv[++i]);
        } else if (arg == "--frames" && i + 1 < argc) {
            // With --screenshot: run this many frames before the image is
            // written, and report what they cost.
            launch.screenshot_after_frames = std::atoi(argv[++i]);
            launch.report_frames = true;
        } else if (arg == "--perf") {
            launch.perf = true;
        } else if (arg == "--seats" && i + 1 < argc) {
            // An offline game of this many seats, the computer in the rest.
            launch.seats = std::atoi(argv[++i]);
        } else if (arg == "--join" && i + 1 < argc) {
            // Joins the network game at HOST:PORT; the seat and the game come
            // from the host.
            launch.join_address = argv[++i];
            launch.direct = true;
        } else if (arg == "--no-ai") {
            // Nobody plays the other seat.
            launch.no_ai = true;
        } else if (arg == "--ai-only") {
            // The computer plays every seat, the player's too: to watch.
            launch.ai_only = true;
        } else if (arg == "--regicide") {
            // A Regicide game: a King and a Castle each, lost with the King.
            setup.regicide = true;
        } else if (arg == "--deathmatch") {
            // A Death Match: 20,000 of every resource.
            setup.resources = Core::StartingResources::DeathMatch;
        } else if (arg == "--population" && i + 1 < argc) {
            // The lobby's population limit, 25 to 200.
            setup.population_limit = std::clamp(std::atoi(argv[++i]), 25, 200);
        } else if (arg == "--all-techs") {
            setup.all_techs = true;
        } else if (arg == "--cheats") {
            // The lobby's Allow Cheats.
            setup.cheats = true;
        } else if (arg == "--age" && i + 1 < argc) {
            // The lobby's starting age: dark, feudal, castle or imperial.
            const std::string age = argv[++i];
            setup.starting_age = age == "feudal" ? 1 : age == "castle" ? 2
                               : age == "imperial" ? 3 : 0;
        } else if (arg == "--difficulty" && i + 1 < argc) {
            // The computer players' difficulty, as the lobby's list orders
            // it: hardest, hard, moderate, easy, easiest.
            const std::string d = argv[++i];
            setup.difficulty = d == "hardest" ? 0 : d == "hard" ? 1 : d == "moderate" ? 2
                             : d == "easiest" ? 4 : 3;
        }
    }
    // --screenshot alone starts a game from the command line; with a menu,
    // movie, slides or the editor, the image is of that instead.
    if (!launch.screenshot_path.empty() && launch.menu.empty() && launch.movie.empty() &&
        launch.slides.empty() && !launch.editor) {
        launch.direct = true;
    }
    // --screenshot runs windowed at the game's size unless --fullscreen is
    // given.
    if (!launch.screenshot_path.empty()) {
        bool fullscreen_requested = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--fullscreen") fullscreen_requested = true;
        }
        win_config.fullscreen = fullscreen_requested;
    }

    // The opponent is the computer's unless told otherwise; with --ai-only the
    // player's own seat too. In a network game the seats are people's.
    setup.players[2].computer = !launch.no_ai && launch.host_port == 0 &&
                                launch.join_address.empty();
    if (launch.ai_only) setup.players[1].computer = true;
    for (int seat = 3; seat <= launch.host_players && launch.host_port != 0; ++seat) {
        setup.players[seat] = Core::PlayerSetup{true, 1 + seat % 13, 0, false};
    }
    // --seats N, offline: the computer takes seats 3 to N as well.
    for (int seat = 3; seat <= std::min(launch.seats, Core::kMaxPlayers - 1) &&
                       launch.host_port == 0 && launch.join_address.empty(); ++seat) {
        setup.players[seat] = Core::PlayerSetup{true, 1 + seat % 13, 0, true};
    }
    // A scenario's seats are its own: last, over the lobby's.
    if (!launch.scenario_path.empty()) {
        std::string path = launch.scenario_path;
        int entry = -1;
        const size_t colon = path.rfind(':');
        if (colon != std::string::npos && colon + 1 < path.size() &&
            path.find_first_not_of("0123456789", colon + 1) == std::string::npos) {
            entry = std::atoi(path.c_str() + colon + 1);
            path.resize(colon);
        }
        std::string why;
        if (!Scenario::SetupFor(path, entry, setup, &why)) {
            std::cerr << "[App] --scenario " << launch.scenario_path << ": " << why << std::endl;
        }
    }
}

// The test flags that set up a state before the first frame: something
// selected, a queue started, a battle laid out. Leaves the view's centre in
// (mcx, mcy).
void ApplyCaptureFlags(ECS::Simulation& sim, const Launch& launch, int& mcx, int& mcy) {
    // Laid first, so --select can pick one of them.
    if (!launch.place_name.empty()) {
        auto& grid = sim.GetGrid();
        const int unit = Core::ConfigManager::Get().GetUnitIdByName(launch.place_name);
        bool placed = false;
        for (int r = 5; r < 20 && !placed && unit >= 0; ++r) {
            for (int dy = -r; dy <= r && !placed; ++dy) {
                for (int dx = -r; dx <= r && !placed; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    if (ECS::Buildings::CanPlace(grid, unit, mcx + dx, mcy + dy)) {
                        ECS::Buildings::Place(sim.GetRegistry(), grid, unit, Core::LocalPlayer(),
                                              mcx + dx, mcy + dy, true);
                        placed = true;
                    }
                }
            }
        }
    }
    if (launch.walls) {
        auto& cfg = Core::ConfigManager::Get();
        auto& reg = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        const Core::PlayerId me = Core::LocalPlayer();
        auto lay = [&](const char* name, int x0, int y0, int x1, int y1) {
            const int unit = cfg.GetUnitIdByName(name);
            for (const auto& [x, y] : ECS::Walls::Line(x0, y0, x1, y1)) {
                if (ECS::Buildings::CanPlace(grid, unit, x, y)) {
                    ECS::Buildings::Place(reg, grid, unit, me, x, y, true);
                }
            }
        };
        lay("WALL", mcx + 6, mcy - 4, mcx + 6, mcy + 2);
        lay("WALL", mcx + 6, mcy + 2, mcx + 11, mcy + 2);
        lay("WALL", mcx + 8, mcy - 6, mcx + 12, mcy - 2);
        lay("WALL2", mcx - 4, mcy + 7, mcx + 6, mcy + 7);
        lay("WALL2", mcx + 13, mcy - 4, mcx + 13, mcy + 6);
        const int tower = cfg.GetUnitIdByName("WCTW");
        if (ECS::Buildings::CanPlace(grid, tower, mcx + 7, mcy + 7)) {
            ECS::Buildings::Place(reg, grid, tower, me, mcx + 7, mcy + 7, true);
        }
        lay("WALL2", mcx + 8, mcy + 7, mcx + 12, mcy + 7);
        for (const auto& [gx, gy] : {std::pair<int, int>{mcx + 1, mcy + 7},
                                     std::pair<int, int>{mcx + 13, mcy + 1}}) {
            const int gate = ECS::Walls::GateFor(reg, me, cfg.GetUnitIdByName("GTAX2"), gx, gy);
            if (ECS::Walls::CanPlaceGate(reg, grid, me, gate, gx, gy)) {
                ECS::Walls::ClearForGate(reg, grid, me, gate, gx, gy);
                ECS::Buildings::Place(reg, grid, gate, me, gx, gy, true);
            }
        }
        mcx += 6;
        mcy += 3;
    }
    // --select: the nearest match to the start, selected and centred on.
    if (!launch.select_name.empty()) {
        const int wanted = Core::ConfigManager::Get().GetUnitIdByName(launch.select_name);
        auto& reg = sim.GetRegistry();
        auto pickable = reg.view<ECS::Components::Identity,
                                 ECS::Components::Selectable,
                                 ECS::Components::Transform>();
        entt::entity found = entt::null;
        float nearest = 1e9f;
        for (auto e : pickable) {
            if (pickable.get<ECS::Components::Identity>(e).unit_id != wanted) continue;
            const auto& at = pickable.get<ECS::Components::Transform>(e);
            const float dx = at.position.x.ToFloat() - static_cast<float>(mcx);
            const float dy = at.position.y.ToFloat() - static_cast<float>(mcy);
            if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; found = e; }
        }
        // Nothing selectable by that name: frame the nearest thing of it that
        // cannot be selected -- a cliff piece, a rock -- without selecting it.
        if (found == entt::null) {
            auto placed = reg.view<ECS::Components::Identity, ECS::Components::Transform>();
            for (auto e : placed) {
                if (placed.get<ECS::Components::Identity>(e).unit_id != wanted) continue;
                const auto& at = placed.get<ECS::Components::Transform>(e);
                const float dx = at.position.x.ToFloat() - static_cast<float>(mcx);
                const float dy = at.position.y.ToFloat() - static_cast<float>(mcy);
                if (dx * dx + dy * dy < nearest) { nearest = dx * dx + dy * dy; found = e; }
            }
            if (found == entt::null) {
                std::cerr << "[App] --select: no " << launch.select_name << " (" << wanted << ") on the map" << std::endl;
            } else {
                const auto& at = placed.get<ECS::Components::Transform>(found);
                mcx = at.position.x.ToInt();
                mcy = at.position.y.ToInt();
            }
        } else {
            pickable.get<ECS::Components::Selectable>(found).is_selected = true;
            const auto& at = pickable.get<ECS::Components::Transform>(found);
            mcx = at.position.x.ToInt();
            mcy = at.position.y.ToInt();

            // --train: queued by the same command the panel's button gives,
            // for the first unit the player has on a button here.
            int trained = -1;
            const auto& units = Core::Rules::Get().units;
            for (int unit_id = 0; unit_id < static_cast<int>(units.size()); ++unit_id) {
                const auto* rules = ECS::Players::UnitOf(reg, Core::LocalPlayer(), unit_id);
                if (rules != nullptr && rules->enabled && rules->train_loc == wanted &&
                    rules->button >= 1) {
                    trained = unit_id;
                    break;
                }
            }
            for (int n = 0; n < launch.train_count && trained >= 0; ++n) {
                Core::CommandQueue::Get().PushCommand(
                    Core::TrainCommand{found, trained, Core::LocalPlayer()});
            }
            if (launch.hurt_percent > 0) {
                if (auto* hp = reg.try_get<ECS::Components::Health>(found)) {
                    hp->current_hp = std::max(1, hp->max_hp * (100 - launch.hurt_percent) / 100);
                }
            }
            if (launch.garrison_count > 0) {
                int sent = 0;
                auto villagers = reg.view<ECS::Components::Worker, ECS::Components::Owner>();
                for (auto v : villagers) {
                    if (sent >= launch.garrison_count) break;
                    if (villagers.get<ECS::Components::Owner>(v).player_id != Core::LocalPlayer()) {
                        continue;
                    }
                    Core::CommandQueue::Get().PushCommand(
                        Core::GarrisonCommand{v, found, Core::LocalPlayer()});
                    ++sent;
                }
            }
            if (launch.rally) {
                Core::CommandQueue::Get().PushCommand(Core::RallyCommand{
                    found, at.position.x + Core::Fixed32(4),
                    at.position.y - Core::Fixed32(2), entt::null, Core::LocalPlayer()});
            }
        }
    }
    if (launch.battle_size > 0) {
        auto& cfg = Core::ConfigManager::Get();
        const int fighter = cfg.GetUnitIdByName("SPRMN");
        const int shooter = cfg.GetUnitIdByName("ARCHR");
        for (int side = 0; side < 2; ++side) {
            const Core::PlayerId player = static_cast<Core::PlayerId>(side + 1);
            const int front = side == 0 ? 7 : 10;
            const int rear = side == 0 ? 5 : 12;
            for (int n = 0; n < launch.battle_size; ++n) {
                const Core::Fixed32 across =
                    Core::Fixed32(mcy) + Core::Fixed32(n) * Core::Fixed32(7) / Core::Fixed32(10);
                ECS::Units::Spawn(sim.GetRegistry(), fighter, player,
                                  {Core::Fixed32(mcx + front), across + Core::Fixed32(5)});
                ECS::Units::Spawn(sim.GetRegistry(), shooter, player,
                                  {Core::Fixed32(mcx + rear), across + Core::Fixed32(5)});
            }
        }
        mcx += 8;
        mcy += 6;
    }
    if (!launch.select_all_name.empty()) {
        const int wanted = Core::ConfigManager::Get().GetUnitIdByName(launch.select_all_name);
        auto& reg = sim.GetRegistry();
        auto pickable = reg.view<ECS::Components::Identity, ECS::Components::Selectable,
                                 ECS::Components::Owner>();
        for (auto e : pickable) {
            if (pickable.get<ECS::Components::Identity>(e).unit_id == wanted &&
                pickable.get<ECS::Components::Owner>(e).player_id == Core::LocalPlayer()) {
                pickable.get<ECS::Components::Selectable>(e).is_selected = true;
            }
        }
    }
    if (launch.look_x >= 0 && launch.look_y >= 0) {
        mcx = launch.look_x;
        mcy = launch.look_y;
    }
    if (launch.build_menu != 0) Rendering::UIRenderer::OpenBuildMenu(sim, launch.build_menu);
}

void WriteMapImage(ECS::Simulation& sim, const std::string& path) {
    const auto& grid = sim.GetGrid();
    constexpr int kScale = 3;
    const int w = grid.GetWidth() * kScale;
    const int h = grid.GetHeight() * kScale;
    SDL_Surface* image = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    auto& graphics = HAL::GraphicCache::Get();
    auto* pixels = static_cast<uint32_t*>(image->pixels);
    for (int y = 0; y < grid.GetHeight(); ++y) {
        for (int x = 0; x < grid.GetWidth(); ++x) {
            const auto& tile = grid.GetTile(x, y);
            const auto& art = Core::ConfigManager::Get().GetTerrainArt(tile.terrain);
            HAL::PaletteColor c = graphics.GetPaletteColor(static_cast<size_t>(art.minimap_index));
            if (!tile.is_walkable && tile.has_tree) c = {20, 70, 20};
            else if (tile.blocked) c = {90, 80, 70};
            for (const auto& start : sim.player_starts) {
                if (std::abs(start.first - x) <= 1 && std::abs(start.second - y) <= 1) c = {255, 0, 0};
            }
            const uint32_t argb = 0xFF000000u | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
            for (int sy = 0; sy < kScale; ++sy) {
                for (int sx = 0; sx < kScale; ++sx) {
                    pixels[(y * kScale + sy) * (image->pitch / 4) + x * kScale + sx] = argb;
                }
            }
        }
    }
    SDL_SaveBMP(image, path.c_str());
    SDL_FreeSurface(image);
    std::cout << "[App] Wrote map image " << path << std::endl;
}

using Micros = std::chrono::duration<int64_t, std::micro>;

// The scripted clicks still to give, and frames until the next.
struct Script {
    std::vector<Launch::Step> clicks;
    size_t next = 0;
    int wait = 10;
};
Script g_script;
constexpr int kFramesPerClick = 8;

// The next scripted click, as the pair of events a mouse would send, in the
// frame's own pixels; false when none is due this frame.
bool ScriptedClick(SDL_Event& down, SDL_Event& up) {
    if (g_script.next >= g_script.clicks.size()) return false;
    if (--g_script.wait > 0) return false;
    g_script.wait = kFramesPerClick;
    const Launch::Step& step = g_script.clicks[g_script.next++];
    down = SDL_Event{};
    if (!step.text.empty()) {
        down.type = SDL_TEXTINPUT;
        std::snprintf(down.text.text, sizeof(down.text.text), "%s", step.text.c_str());
        up = SDL_Event{};
        up.type = SDL_USEREVENT;
        return true;
    }
    if (step.key != SDLK_UNKNOWN) {
        // The modifiers are what the handlers read off the event; the key
        // state is not consulted for them.
        down.type = SDL_KEYDOWN;
        down.key.keysym.sym = step.key;
        down.key.keysym.scancode = SDL_GetScancodeFromKey(step.key);
        down.key.keysym.mod = step.mod;
        down.key.state = SDL_PRESSED;
        up = down;
        up.type = SDL_KEYUP;
        up.key.state = SDL_RELEASED;
        return true;
    }
    if (step.move) {
        if (HAL::Window* window = HAL::MainWindow()) window->HoldMouse(step.at.x, step.at.y);
        down.type = SDL_MOUSEMOTION;
        down.motion.x = step.at.x;
        down.motion.y = step.at.y;
        up = SDL_Event{};
        up.type = SDL_USEREVENT;
        return true;
    }
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.button = step.button;
    down.button.x = step.at.x;
    down.button.y = step.at.y;
    up = down;
    up.type = SDL_MOUSEBUTTONUP;
    if (step.held) {
        if (HAL::Window* window = HAL::MainWindow()) window->HoldMouse(step.at.x, step.at.y);
        up = SDL_Event{};
        up.type = SDL_USEREVENT;
    }
    return true;
}

bool ScriptDone() { return g_script.next >= g_script.clicks.size(); }

// Ceiling on how often a frame is drawn. The simulation runs a fixed step,
// so faster frames gain nothing.
constexpr int kFpsCap = 120;
constexpr Micros kMinFrame{1000000 / kFpsCap};

void HoldFrame(std::chrono::high_resolution_clock::time_point started) {
    // Sleeps out the rest of the frame.
    const auto spent = std::chrono::duration_cast<Micros>(
        std::chrono::high_resolution_clock::now() - started);
    if (spent < kMinFrame) std::this_thread::sleep_for(kMinFrame - spent);
}

// The menus, at 800x600, until they ask for something.
Screens::Request RunMenus(HAL::Window& window, const Launch& launch) {
    window.SetGameSize(Screens::kMenuWidth, Screens::kMenuHeight);
    SDL_Renderer* sdl_r = window.GetSDLRenderer();
    int frames_left = launch.screenshot_after_frames;
    Audio::Music::Menus();
    Screens::Id showing = Screens::Current();
    while (true) {
        const auto started = std::chrono::high_resolution_clock::now();
        // The credits have a track of their own; leaving them, the menus'.
        if (Screens::Current() != showing) {
            if (Screens::Current() == Screens::Id::Credits) Audio::Music::Credits();
            else if (showing == Screens::Id::Credits) Audio::Music::Menus();
            showing = Screens::Current();
        }
        Audio::Music::Update();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                Screens::Request quit;
                quit.kind = Screens::Request::Kind::Quit;
                return quit;
            }
            window.TranslateEvent(event);
            Screens::HandleEvent(event);
        }
        SDL_Event down, up;
        if (ScriptedClick(down, up)) {
            Screens::HandleEvent(down);
            Screens::HandleEvent(up);
        }
        Screens::Request request = Screens::TakeRequest();
        if (request.kind != Screens::Request::Kind::None) return request;

        window.BeginFrame();
        window.Clear(0, 0, 0);
        Screens::Draw(sdl_r);
        // The plain pointer, on the menus' own cursor art.
        int mx = 0, my = 0;
        window.MouseState(mx, my);
        if (const HAL::GraphicAtlas* cursors = HAL::GraphicCache::Get().GetInterfaceAtlas(51000);
            cursors != nullptr && cursors->base != nullptr && !cursors->frames.empty()) {
            const HAL::GraphicFrame& f = cursors->frames[0];
            const SDL_Rect dest = {mx - f.hotspot_x, my - f.hotspot_y, f.rect.w, f.rect.h};
            if (launch.screenshot_path.empty()) SDL_RenderCopy(sdl_r, cursors->base, &f.rect, &dest);
        }
        if (!launch.screenshot_path.empty() && ScriptDone() && --frames_left <= 0) {
            if (window.CaptureFrame(launch.screenshot_path)) {
                std::cout << "[App] Wrote screenshot " << launch.screenshot_path << " ("
                          << window.GetWidth() << "x" << window.GetHeight() << ")" << std::endl;
            }
            Screens::Request quit;
            quit.kind = Screens::Request::Kind::Quit;
            return quit;
        }
        window.Present();
        HoldFrame(started);
    }
}

// How a game ended, for whoever started it.
enum class GameEnd { Quit, ToMenu, Restart, LoadMenu };

// What a game starts from, besides its lobby: a saved game to open on, or a
// recording to watch. `won`, when given, is set if the player at this machine
// wins it -- what a campaign's progress is told.
struct Play {
    std::string load_path;
    const Save::Recording* playback = nullptr;
    bool* won = nullptr;
};

// A screen of the menus shown in the midst of a game -- a scenario's
// instructions before it, its aftermath after -- at the menus' 800x600,
// until its button is pressed. False when the player closes the window
// instead (or once --screenshot has written its image).
bool RunScreen(HAL::Window& window, const Launch& launch) {
    window.SetGameSize(Screens::kMenuWidth, Screens::kMenuHeight);
    SDL_Renderer* sdl_r = window.GetSDLRenderer();
    int frames_left = launch.screenshot_after_frames;
    while (true) {
        const auto started = std::chrono::high_resolution_clock::now();
        Audio::Music::Update();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) return false;
            window.TranslateEvent(event);
            Screens::HandleEvent(event);
        }
        SDL_Event down, up;
        if (ScriptedClick(down, up)) {
            Screens::HandleEvent(down);
            Screens::HandleEvent(up);
        }
        const Screens::Request request = Screens::TakeRequest();
        if (request.kind == Screens::Request::Kind::Begin) return true;
        if (request.kind == Screens::Request::Kind::Quit) return false;

        window.BeginFrame();
        window.Clear(0, 0, 0);
        Screens::Draw(sdl_r);
        int mx = 0, my = 0;
        window.MouseState(mx, my);
        if (const HAL::GraphicAtlas* cursors = HAL::GraphicCache::Get().GetInterfaceAtlas(51000);
            cursors != nullptr && cursors->base != nullptr && !cursors->frames.empty() &&
            launch.screenshot_path.empty()) {
            const HAL::GraphicFrame& f = cursors->frames[0];
            const SDL_Rect dest = {mx - f.hotspot_x, my - f.hotspot_y, f.rect.w, f.rect.h};
            SDL_RenderCopy(sdl_r, cursors->base, &f.rect, &dest);
        }
        if (!launch.screenshot_path.empty() && ScriptDone() && --frames_left <= 0) {
            if (window.CaptureFrame(launch.screenshot_path)) {
                std::cout << "[App] Wrote screenshot " << launch.screenshot_path << " ("
                          << window.GetWidth() << "x" << window.GetHeight() << ")" << std::endl;
            }
            return false;
        }
        window.Present();
        HoldFrame(started);
    }
}

// The scenario's instructions (the Mission Dialog), shown once the game is
// made and before it runs.
bool RunMission(HAL::Window& window, const Scenario::File& file, const Launch& launch) {
    Screens::OpenMission(file);
    return RunScreen(window, launch);
}

// A show of campaign slides at 800x600, until it ends or the player skips
// it: a mouse button, Escape or Space. Its files missing, it is over at once.
// Given a time in milliseconds as its last argument, the show is stepped to
// that time and its frame written to the launch's image path instead, and
// false returned as for a closed window.
bool RunSlides(HAL::Window& window, const Screens::Slides::Show& show, const Launch& launch,
               int capture_at = -1) {
    namespace Slides = Screens::Slides;
    const bool capture = capture_at >= 0;
    SDL_Renderer* sdl_r = window.GetSDLRenderer();
    window.SetGameSize(Screens::kMenuWidth, Screens::kMenuHeight);
    const bool sound = !launch.mute && launch.screenshot_path.empty();
    if (!Slides::Start(show, sound, sdl_r)) return true;
    std::cout << "[App] Slides " << show.name << std::endl;
    // The narration is the show's only sound.
    HAL::Audio::StopMusic();
    if (capture) {
        for (int t = 0; t <= capture_at && !Slides::Done(); t += 10) {
            Slides::Advance(static_cast<uint32_t>(t));
        }
        window.BeginFrame();
        window.Clear(0, 0, 0);
        Slides::Draw(sdl_r);
        if (window.CaptureFrame(launch.screenshot_path)) {
            std::cout << "[App] Wrote screenshot " << launch.screenshot_path << " ("
                      << window.GetWidth() << "x" << window.GetHeight() << ")" << std::endl;
        }
        Slides::Stop();
        return false;
    }
    const Uint32 started = SDL_GetTicks();
    auto skips = [](const SDL_Event& e) {
        return e.type == SDL_MOUSEBUTTONDOWN ||
               (e.type == SDL_KEYDOWN &&
                (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_SPACE));
    };
    while (!Slides::Done()) {
        const auto frame_start = std::chrono::high_resolution_clock::now();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                Slides::Stop();
                return false;
            }
            window.TranslateEvent(event);
            if (skips(event)) Slides::Skip();
        }
        SDL_Event down, up;
        if (ScriptedClick(down, up) && skips(down)) Slides::Skip();
        Slides::Advance(SDL_GetTicks() - started);
        window.BeginFrame();
        window.Clear(0, 0, 0);
        Slides::Draw(sdl_r);
        window.Present();
        HoldFrame(frame_start);
    }
    Slides::Stop();
    std::cout << "[App] Slides over" << std::endl;
    return true;
}

// A show by its script's name: "c1s1_beg", "c8s7_end", or a custom one's.
Screens::Slides::Show ShowNamed(const std::string& name) {
    int campaign = 0, scenario = 0;
    char part[4] = {};
    if (std::sscanf(name.c_str(), "c%ds%d_%3s", &campaign, &scenario, part) == 3) {
        return Screens::Slides::OfScenario(campaign, scenario, std::string(part) == "beg");
    }
    return Screens::Slides::Custom(name);
}

// Which of the built-in campaigns a scenario game is from, and which of its
// scenarios (from 1): 0 for a scenario of its own.
struct CampaignGame {
    int number = 0;
    int scenario = 0;
};
CampaignGame CampaignOf(const Core::GameSetup& setup) {
    CampaignGame game;
    if (setup.scenario.empty() || setup.scenario_entry < 0) return game;
    Scenario::Campaign campaign;
    if (!Scenario::LoadCampaign(setup.scenario, campaign)) return game;
    game.number = Scenario::CampaignNumber(campaign);
    game.scenario = setup.scenario_entry + 1;
    return game;
}

// The camera on a tile, as a minimap click puts it there: a Change View
// effect's.
void LookAtTile(Rendering::Renderer& renderer, const HAL::Window& window, int x, int y) {
    const auto at = Core::IsometricMath::WorldToScreen(Core::Fixed32(x), Core::Fixed32(y));
    renderer.target_camera_x = at.x - (window.GetWidth() / 2.0f) / renderer.target_camera_zoom;
    renderer.target_camera_y = at.y - (window.GetHeight() / 2.0f) / renderer.target_camera_zoom;
    renderer.camera_x = renderer.target_camera_x;
    renderer.camera_y = renderer.target_camera_y;
}

// Where a recording of this game goes: "name - date time.mgl" in the saved
// games folder.
std::string RecordingPath(const Core::GameSetup& setup) {
    char when[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof(when), "%Y-%m-%d %H%M%S", std::localtime(&now));
    std::string who = setup.players[Core::LocalPlayer()].name;
    if (who.empty()) who = "Player";
    return Screens::SavedGamesDir() + who + " - " + when + ".mgl";
}

GameEnd RunGame(HAL::Window& window, Rendering::Renderer& renderer, Core::GameSetup setup,
                Net::Session& session, const Launch& launch, const Play& play = {}) {
    const auto& profile = Core::Profile::Current();
    const int game_h = launch.direct ? launch.window.game_height : profile.screen_height;
    // Widescreen: as wide as the screen at this height, the interface centred.
    const int game_w = launch.frame_width > 0 ? launch.frame_width
                       : launch.widescreen || profile.widescreen
                           ? window.WideWidth(game_h)
                           : launch.direct ? launch.window.game_width : profile.screen_width;
    window.SetGameSize(game_w, game_h);
    GameDialogs::Reset();
    Rendering::UIRenderer::NewGame();
    Rendering::EntityRenderer::ClearMarkers();
    Rendering::EntityRenderer::ClearFlashes();
    Rendering::Alerts::Reset();
    // The ground baked for the last game is not this one's.
    Rendering::TerrainRenderer::InvalidateChunks();
    Rendering::Messages::Reset();
    Core::CommandQueue::Get().PopAllCommands();
    Audio::Music::Game();

    // === Simulation ===
    if (!profile.name.empty() && Core::LocalPlayer() < Core::kMaxPlayers &&
        !setup.players[Core::LocalPlayer()].computer &&
        setup.players[Core::LocalPlayer()].name.empty()) {
        setup.players[Core::LocalPlayer()].name = profile.name;
    }
    // An offline game always allows cheats, whatever started it; only the
    // multiplayer lobby has an "Allow Cheats" box (string 13526).
    if (session.role == Net::Role::Offline) setup.cheats = true;
    auto sim = std::make_unique<ECS::Simulation>();
    if (play.playback != nullptr) setup = play.playback->setup;
    sim->Init(setup);
    if (!play.load_path.empty() || !session.restore.empty()) {
        // A saved game: the world it was, the lobby it was started from --
        // for a restored network game, the save the host handed everyone.
        std::string why;
        const bool read = session.restore.empty() ? Save::ReadFile(*sim, play.load_path, &why)
                                                  : Save::Read(*sim, session.restore, &why);
        if (!read) {
            std::cerr << "[App] " << Core::Strings::Get(9473) << " (" << why << ")" << std::endl;
            return GameEnd::ToMenu;
        }
        setup = sim->Setup();
        Rendering::Messages::CatchUp(sim->GetRegistry());
        Audio::CatchUp(sim->GetRegistry());
    }
    // The colours the lobby gave the players, for their units, the mini-map
    // and their lines of text.
    std::array<int, 16> colours{};
    for (int p = 0; p < 16; ++p) colours[static_cast<size_t>(p)] = p < Core::kMaxPlayers ? setup.players[p].colour : -1;
    HAL::GraphicCache::Get().SetPlayerColours(colours);
    Core::ConfigManager::Get().SetPlayerColours(colours);
    // Recorded as it is played, when the lobby asked; a game being watched is
    // not recorded again.
    Save::Recording recording;
    const bool recording_on = setup.record && play.playback == nullptr && play.load_path.empty();
    if (recording_on) {
        recording.setup = setup;
        recording.viewer = Core::LocalPlayer();
    }
    size_t playback_turn = 0;
    size_t playback_check = 0;
    bool playback_parted = false;

    if (!launch.map_image_path.empty()) {
        WriteMapImage(*sim, launch.map_image_path);
        return GameEnd::Quit;
    }

    // Open on the player's Town Center, or the map centre if no start was
    // generated.
    int mcx = sim->GetGrid().GetWidth() / 2;
    int mcy = sim->GetGrid().GetHeight() / 2;
    // The local player's own start: starts are in seat order.
    {
        size_t index = 0;
        for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
            if (!setup.players[seat].active) continue;
            if (seat == Core::LocalPlayer()) break;
            ++index;
        }
        if (index < sim->player_starts.size()) {
            mcx = sim->player_starts[index].first;
            mcy = sim->player_starts[index].second;
        }
    }
    ApplyCaptureFlags(*sim, launch, mcx, mcy);
    // --advance: the simulation's own fixed tick, run ahead of the first frame.
    // AOE_AI_TRACE=1 prints the computer players minute by minute meanwhile.
    {
        const bool trace = std::getenv("AOE_AI_TRACE") != nullptr;
        const int ticks = launch.advance_seconds * 1000 / 16;
        for (int t = 1; t <= ticks; ++t) {
            sim->Update(16);
            if (!trace || t % Verify::AIGame::kTicksPerMinute != 0) continue;
            for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
                if (setup.players[p].active) Verify::AIGame::Trace(sim->GetRegistry(), t / Verify::AIGame::kTicksPerMinute, p);
            }
        }
    }
    if (launch.achievements_page > 0) {
        if (launch.achievements_end) GameDialogs::GameOver(true, session.role == Net::Role::Offline);
        GameDialogs::Open(GameDialogs::Dialog::Achievements);
        GameDialogs::ShowAchievementsPage(launch.achievements_page);
    }
    // The world's art decoded now, while the loading screen is up, not as it
    // first comes into view.
    HAL::Platform::LogMemory("before the art");
    if (HAL::Platform::WarmArt()) Rendering::Renderer::Warm(*sim);
    HAL::Platform::LogMemory("with the art");

    // A single-player game's message lines sit below the place a scenario's
    // instructions take. Before a scenario, in a single-player game started
    // afresh: a campaign's scenario has its slides, William Wallace's first
    // also the intro before them once each time the campaign's map was
    // opened, and goes straight on into the game after them; any other
    // scenario has its instructions screen, when
    // it has instructions. --screenshot skips the instructions unless
    // --mission asks for them.
    const Scenario::File* scenario = ECS::Triggers::ScenarioOf(sim->GetRegistry());
    const CampaignGame campaign = scenario != nullptr ? CampaignOf(setup) : CampaignGame{};
    Rendering::Messages::SetSinglePlayer(session.role == Net::Role::Offline);
    const bool fresh = scenario != nullptr && session.role == Net::Role::Offline &&
                       play.load_path.empty() && play.playback == nullptr;
    // With --screenshot they play only while --clicks has clicks to give.
    if (fresh && campaign.number > 0 && (launch.screenshot_path.empty() || !ScriptDone())) {
        if (campaign.number == 8 && campaign.scenario == 1 && Screens::Slides::TakeIntro() &&
            !RunSlides(window, Screens::Slides::Custom("intro"), launch)) {
            return GameEnd::Quit;
        }
        if (!RunSlides(window, Screens::Slides::OfScenario(campaign.number, campaign.scenario, true),
                       launch)) {
            return GameEnd::Quit;
        }
        window.SetGameSize(game_w, game_h);
        Audio::Music::Game();
    } else if (fresh && campaign.number == 0 &&
               !Scenario::Show(scenario->texts[Scenario::kInstructions]).empty() &&
               (launch.screenshot_path.empty() || launch.mission)) {
        if (!RunMission(window, *scenario, launch)) return GameEnd::Quit;
        window.SetGameSize(game_w, game_h);
    }
    bool won_game = false;
    // While the scenario asks whether something is selected, it is told what
    // is each time that changes: a SelectionCommand, in lockstep like any
    // order.
    const bool watches_selection = ECS::Triggers::WatchesSelection(sim->GetRegistry()) &&
                                   play.playback == nullptr;
    std::vector<entt::entity> told_selection;
    // The scenario's display lines seen so far: Change View moves the camera.
    uint64_t seen_display = 0;
    if (const auto* display = ECS::Triggers::ReadDisplay(sim->GetRegistry());
        display != nullptr && !play.load_path.empty()) {
        for (const auto& line : display->lines) seen_display = std::max(seen_display, line.tick);
    }
    auto center = Core::IsometricMath::WorldToScreen(Core::Fixed32(mcx), Core::Fixed32(mcy));
    if (launch.zoom > 0.0f) {
        renderer.camera_zoom = launch.zoom;
        renderer.target_camera_zoom = launch.zoom;
    }
    renderer.camera_x = center.x - window.GetWidth() / 2.0f / renderer.camera_zoom;
    renderer.camera_y = center.y - window.GetHeight() / 2.0f / renderer.camera_zoom;
    // The input system eases the camera toward its target every frame, so the
    // target starts where the camera is.
    renderer.target_camera_x = renderer.camera_x;
    renderer.target_camera_y = renderer.camera_y;
    renderer.banner.clear();

    // The accumulator is kept in microseconds: whole milliseconds would drop
    // each frame's remainder, slowing the simulation as the frame rate rises.
    constexpr int32_t kTickMs = 16;                       // ~60 Hz simulation
    constexpr Micros kTick{kTickMs * 1000};
    constexpr Micros kMaxFrame{250000};                   // spiral-of-death cap
    // The most a frame spends on the simulation offline: a machine that cannot
    // keep up plays the game slower, rather than showing it seldom.
    constexpr Micros kSimBudget{25000};

    // The game's speed: Slow, Normal and Fast run the world at 1, 1.5 and 2
    // times the dat's own rates. --screenshot runs at the dat's own.
    int speed_percent = launch.screenshot_path.empty()
        ? Core::Profile::kGameSpeedPercent[std::clamp(profile.game_speed, 0, 2)] : 100;

    auto prev_time = std::chrono::high_resolution_clock::now();
    Micros accumulator{0};
    int fps_frames = 0;
    float fps_timer = 0.0f;
    int screenshot_after_frames = launch.screenshot_after_frames;
    struct FrameReport {
        int seen = 0;
        int frames = 0;
        int slow = 0;          // over a 60 Hz frame
        Micros total{0};
        Micros worst{0};
        Micros warmup{0};
    } frame_report;
    constexpr int kWarmupFrames = 60;
    // --perf: a second's frames, their costs summed in microseconds.
    struct PerfReport {
        int frames = 0;
        uint64_t ticks = 0;
        int64_t sim = 0;
        int64_t frame = 0;
        Rendering::Renderer::Cost drawn;
    } perf;
    bool told_outcome = false;
    GameEnd end = GameEnd::Quit;

    bool running = true;
    // A saved single-player game takes them too: the lobby it was saved
    // with is read back over the one above.
    if (session.role == Net::Role::Offline) {
        if (auto* in_force = sim->GetRegistry().ctx().find<Core::GameSetup>()) in_force->cheats = true;
    }
    while (running) {
        auto now = std::chrono::high_resolution_clock::now();
        auto frame_time = std::chrono::duration_cast<Micros>(now - prev_time);
        prev_time = now;
        if (frame_time > kMaxFrame) frame_time = kMaxFrame;
        const bool paused = session.role == Net::Role::Offline && GameDialogs::Paused();
        if (!paused) accumulator += frame_time * speed_percent / 100;
        const float dt = static_cast<float>(frame_time.count()) / 1000000.0f;

        // === Input ===
        // Typed characters arrive only while text input is on, and it is on
        // only while a line is being typed into: otherwise the game's keys
        // are hotkeys.
        HAL::TextInput::Want(GameDialogs::WantsText() ? HAL::TextInput::Kind::Line
                                                       : HAL::TextInput::Kind::None);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { running = false; end = GameEnd::Quit; }
            // Input sees the game's own pixels, never the window's.
            window.TranslateEvent(event);
            // A dialog over the game takes its input first.
            if (GameDialogs::HandleEvent(event, *sim)) continue;
            // Game speed hotkeys (single player).
            if (event.type == SDL_KEYDOWN && session.role == Net::Role::Offline) {
                if (Core::Hotkeys::Is(event.key, Core::Hotkeys::kGameCommands,
                                      Core::Hotkeys::kSpeedUp)) {
                    speed_percent = speed_percent >= 150 ? 200 : 150;
                } else if (Core::Hotkeys::Is(event.key, Core::Hotkeys::kGameCommands,
                                             Core::Hotkeys::kSlowDown)) {
                    speed_percent = speed_percent <= 150 ? 100 : 150;
                }
            }
            Core::InputManager::Get().ProcessEvent(event, *sim, renderer);
        }
        {
            SDL_Event down, up;
            if (ScriptedClick(down, up)) {
                for (const SDL_Event* e : {&down, &up}) {
                    if (!GameDialogs::HandleEvent(*e, *sim)) {
                        Core::InputManager::Get().ProcessEvent(*e, *sim, renderer);
                    }
                }
            }
        }
        // The top bar's buttons open their screens.
        switch (Rendering::UIRenderer::TakeDialogRequest()) {
        case 0: GameDialogs::Open(GameDialogs::Dialog::Objectives); break;
        case 1: GameDialogs::Open(GameDialogs::Dialog::TechTree); break;
        case 2: GameDialogs::Open(GameDialogs::Dialog::Chat); break;
        case 3: GameDialogs::Open(GameDialogs::Dialog::Diplomacy); break;
        case 4: GameDialogs::Open(GameDialogs::Dialog::Menu); break;
        default: break;
        }
        switch (GameDialogs::TakeOutcome()) {
        case GameDialogs::Outcome::QuitToMenu: running = false; end = GameEnd::ToMenu; break;
        case GameDialogs::Outcome::QuitGame: running = false; end = GameEnd::Quit; break;
        case GameDialogs::Outcome::Restart: running = false; end = GameEnd::Restart; break;
        case GameDialogs::Outcome::Save: {
            // Into the saved games folder under the name given, as a .gam.
            const std::string path = Screens::SavedGamesDir() + GameDialogs::SaveName() + ".gam";
            if (!Save::WriteFile(*sim, path)) Rendering::Messages::Post(Core::Strings::Get(2402));
            break;
        }
        case GameDialogs::Outcome::Load: running = false; end = GameEnd::LoadMenu; break;
        case GameDialogs::Outcome::Resign:
            Core::CommandQueue::Get().PushCommand(Core::ResignCommand{Core::LocalPlayer()});
            break;
        case GameDialogs::Outcome::Ended: {
            // Between the end's message and the achievements: a campaign's
            // scenario won has its closing slides, and William Wallace's go
            // straight back to the campaign's map with no achievements; a
            // scenario of its own has its aftermath, when it has the text for
            // it.
            bool to_achievements = true;
            if (scenario != nullptr && campaign.number > 0) {
                if (won_game && (launch.screenshot_path.empty() || !ScriptDone()) &&
                    !RunSlides(window, Screens::Slides::OfScenario(campaign.number,
                                                                   campaign.scenario, false),
                               launch)) {
                    running = false;
                    end = GameEnd::Quit;
                    break;
                }
                if (campaign.number == 8) {
                    running = false;
                    end = GameEnd::ToMenu;
                    to_achievements = false;
                }
            } else if (scenario != nullptr && Screens::OpenAftermath(*scenario, won_game)) {
                if (!RunScreen(window, launch)) {
                    running = false;
                    end = GameEnd::Quit;
                    break;
                }
            }
            window.SetGameSize(game_w, game_h);
            if (to_achievements) GameDialogs::Open(GameDialogs::Dialog::Achievements);
            break;
        }
        default: break;
        }
        // --screenshot holds the camera still: no edge scrolling.
        if (launch.screenshot_path.empty() && GameDialogs::Current() == GameDialogs::Dialog::None) {
            Core::InputManager::Get().Update(renderer, dt, window.GetWidth(), window.GetHeight());
        }
        Core::InputManager::Get().Hover(*sim, renderer);
        if (watches_selection) {
            std::vector<entt::entity> selected;
            auto view = sim->GetRegistry().view<ECS::Components::Selectable>();
            for (auto e : view) {
                if (view.get<ECS::Components::Selectable>(e).is_selected) selected.push_back(e);
            }
            std::sort(selected.begin(), selected.end());
            if (selected != told_selection) {
                Core::CommandQueue::Get().PushCommand(
                    Core::SelectionCommand{selected, Core::LocalPlayer()});
                told_selection = std::move(selected);
            }
        }

        // === Fixed-step simulation ===
        const auto sim_start = std::chrono::high_resolution_clock::now();
        const uint64_t tick_before = sim->Tick();
        if (session.role == Net::Role::Offline) {
            while (accumulator >= kTick) {
                if (play.playback != nullptr) {
                    // Watching: the recorded orders, not the watcher's.
                    Save::Replay(*play.playback, playback_turn, sim->Tick() + 1);
                } else if (recording_on) {
                    Save::TakeTurn(recording, sim->Tick() + 1);
                }
                sim->Update(kTickMs);
                if (recording_on) Save::Witness(recording, *sim);
                if (play.playback != nullptr && !playback_parted &&
                    !Save::StillTheGame(*play.playback, playback_check, *sim)) {
                    playback_parted = true;
                    std::cerr << "[App] The recording and the game it plays back have parted "
                                 "at tick " << sim->Tick() << std::endl;
                }
                accumulator -= kTick;
                if (accumulator >= kTick && std::chrono::high_resolution_clock::now() - sim_start > kSimBudget) {
                    accumulator = kTick - Micros{1};
                    break;
                }
            }
        } else {
            // In lockstep: this machine's orders go to everyone, and a tick
            // runs only once every player's orders for its turn are in.
            Net::Pump(session);
            for (const auto& command : Core::CommandQueue::Get().PopAllCommands()) {
                Net::Issue(session, command);
            }
            std::vector<Core::Command> due;
            while (accumulator >= kTick) {
                if (!Net::NextTick(session, due)) {
                    if (accumulator > kTick) accumulator = kTick;   // waiting: do not bank time
                    break;
                }
                for (const auto& command : due) Core::CommandQueue::Get().PushCommand(command);
                if (recording_on) Save::TakeTurn(recording, sim->Tick() + 1);
                sim->Update(kTickMs);
                if (recording_on) Save::Witness(recording, *sim);
                Net::TickDone(session, sim->GetRegistry());
                accumulator -= kTick;
            }
            renderer.banner = session.desynced ? Core::Strings::Get(Core::Strings::Id::OutOfSync)
                            : !session.failure.empty() ? session.failure
                            : std::string{};
        }
        const auto sim_end = std::chrono::high_resolution_clock::now();

        // A scenario's Change View, on this player's screen.
        if (const auto* display = ECS::Triggers::ReadDisplay(sim->GetRegistry())) {
            for (const auto& line : display->lines) {
                if (line.tick <= seen_display) continue;
                if (line.kind == ECS::Triggers::Show::View &&
                    (line.player == 0 || line.player == Core::LocalPlayer())) {
                    LookAtTile(renderer, window, line.x, line.y);
                }
            }
            for (const auto& line : display->lines) seen_display = std::max(seen_display, line.tick);
        }

        // The game's end, for the player at this machine.
        if (const auto* outcome = ECS::Victory::Read(sim->GetRegistry()); outcome && !told_outcome) {
            if (outcome->Won(Core::LocalPlayer())) {
                GameDialogs::GameOver(true, session.role == Net::Role::Offline);
                Audio::Music::Ended(true);
                if (play.won != nullptr) *play.won = true;
                won_game = true;
                told_outcome = true;
            } else if (outcome->Defeated(Core::LocalPlayer())) {
                GameDialogs::GameOver(false, session.role == Net::Role::Offline);
                Audio::Music::Ended(false);
                told_outcome = true;
            }
        }
        Audio::Music::Update();

        Rendering::UIRenderer::SetGameSpeed(speed_percent);
        // === Render with interpolation ===
        // How far this frame falls into the next tick.
        const float alpha = static_cast<float>(accumulator.count()) / static_cast<float>(kTick.count());
        renderer.Render(*sim, window, alpha);
        GameDialogs::Draw(window.GetSDLRenderer(), *sim, window.GetWidth(), window.GetHeight());

        // === Sound: what the view hears of this frame ===
        {
            Audio::Listener listener;
            listener.left = renderer.camera_x;
            listener.top = renderer.camera_y;
            listener.width = static_cast<float>(window.GetWidth()) / renderer.camera_zoom;
            listener.height = static_cast<float>(window.GetHeight()) / renderer.camera_zoom;
            Audio::Update(sim->GetRegistry(), listener, Core::LocalPlayer());
            Rendering::Messages::Update(sim->GetRegistry(), Core::LocalPlayer());
            // The minimap's notices, the alarms just sounded among them.
            Rendering::Alerts::Update(sim->GetRegistry(), Core::LocalPlayer());
        }

        // === Frame image (--screenshot) ===
        if (!launch.screenshot_path.empty() && ScriptDone() && --screenshot_after_frames <= 0) {
            // The game's frame itself, at the game's size.
            if (window.CaptureFrame(launch.screenshot_path)) {
                std::cout << "[App] Wrote screenshot " << launch.screenshot_path << " ("
                          << window.GetWidth() << "x" << window.GetHeight() << ")" << std::endl;
            }
            running = false;
            end = GameEnd::Quit;
        }
        renderer.Finish(*sim, window);

        if (launch.perf) {
            const auto end_of_frame = std::chrono::high_resolution_clock::now();
            ++perf.frames;
            perf.ticks += sim->Tick() - tick_before;
            perf.sim += std::chrono::duration_cast<Micros>(sim_end - sim_start).count();
            perf.frame += std::chrono::duration_cast<Micros>(end_of_frame - now).count();
            perf.drawn.terrain += renderer.cost.terrain;
            perf.drawn.ground += renderer.cost.ground;
            perf.drawn.fog += renderer.cost.fog;
            perf.drawn.standing += renderer.cost.standing;
            perf.drawn.interface += renderer.cost.interface;
            perf.drawn.finish += renderer.cost.finish;
        }

        // --frames: what the frame cost, its sleep aside.
        // The first second is the art being decoded on first use; it is
        // reported apart from the frames that follow.
        if (launch.report_frames) {
            const auto spent = std::chrono::duration_cast<Micros>(
                std::chrono::high_resolution_clock::now() - prev_time);
            if (++frame_report.seen <= kWarmupFrames) {
                frame_report.warmup = std::max(frame_report.warmup, spent);
            } else {
                ++frame_report.frames;
                frame_report.total += spent;
                frame_report.worst = std::max(frame_report.worst, spent);
                if (spent > Micros{16667}) ++frame_report.slow;
            }
        }

        // === FPS Tracking ===
        fps_frames++;
        fps_timer += dt;
        if (fps_timer >= 1.0f) {
            renderer.current_fps = fps_frames;
            fps_frames = 0;
            fps_timer -= 1.0f;
            if (launch.perf && perf.frames > 0) {
                const double n = perf.frames;
                std::printf("[Perf] %d fps, %.1f ms a frame: %.1f simulation (%.1f ticks), "
                            "%.1f terrain, %.1f ground, %.1f fog, %.1f standing, %.1f interface, "
                            "%.1f shown; %zu entities\n",
                            perf.frames, perf.frame / n / 1000.0, perf.sim / n / 1000.0, perf.ticks / n,
                            perf.drawn.terrain / n / 1000.0, perf.drawn.ground / n / 1000.0,
                            perf.drawn.fog / n / 1000.0, perf.drawn.standing / n / 1000.0,
                            perf.drawn.interface / n / 1000.0, perf.drawn.finish / n / 1000.0,
                            static_cast<size_t>(sim->GetRegistry().storage<entt::entity>().size()));
                std::fflush(stdout);
                perf = PerfReport{};
            }
        }
        HoldFrame(prev_time);
    }
    if (launch.report_frames && frame_report.frames > 0) {
        std::cout << "[App] " << frame_report.frames << " frames, average "
                  << static_cast<double>(frame_report.total.count()) / frame_report.frames / 1000.0
                  << " ms, worst " << frame_report.worst.count() / 1000.0 << " ms, "
                  << frame_report.slow << " over 16.7 ms; warm-up worst "
                  << frame_report.warmup.count() / 1000.0 << " ms; "
                  << sim->GetRegistry().storage<entt::entity>().in_use() << " entities" << std::endl;
    }
    if (recording_on && !recording.turns.empty()) {
        const std::string path = RecordingPath(setup);
        if (!Save::WriteRecording(recording, path)) {
            std::cerr << "[App] Could not write the recording " << path << std::endl;
        }
    }
    return end;
}

// A network game's lobby, from the command line or the multiplayer screen:
// until everyone is seated, or it fails.
bool RunLobby(Net::Session& session, Core::GameSetup& setup, int host_port, int host_players,
              const std::string& join_address) {
    if (host_port != 0) {
        if (!Net::Host(session, static_cast<uint16_t>(host_port), setup, host_players)) {
            std::cerr << "[Net] " << session.failure << std::endl;
            return false;
        }
        std::cout << "[Net] " << Core::Strings::Get(Core::Strings::Id::WaitingForPlayers)
                  << " (port " << host_port << ")" << std::endl;
        while (!session.started && session.failure.empty()) {
            Net::Pump(session);
            SDL_PumpEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    } else if (!join_address.empty()) {
        const size_t colon = join_address.rfind(':');
        const std::string host = join_address.substr(0, colon);
        const int port = colon == std::string::npos ? 27015 : std::atoi(join_address.c_str() + colon + 1);
        if (!Net::Join(session, host, static_cast<uint16_t>(port))) {
            std::cerr << "[Net] " << session.failure << std::endl;
            return false;
        }
        while (!session.started && session.failure.empty()) {
            Net::Pump(session);
            SDL_PumpEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        setup = session.setup;
        Core::SetLocalPlayer(session.seat);
    }
    if (!session.failure.empty()) {
        std::cerr << "[Net] " << session.failure << std::endl;
        return false;
    }
    if (session.role != Net::Role::Offline) {
        std::cout << "[Net] Playing seat " << int(session.seat) << " of "
                  << session.humans.size() << std::endl;
    }
    return true;
}

// The scenario editor, at the profile's screen size, until it is left: back
// to the menus, or the window closed. A test game is played from it and
// comes back to it.
GameEnd RunEditor(HAL::Window& window, Rendering::Renderer& renderer, const Launch& launch,
                  const std::string& path, bool choose) {
    const auto& profile = Core::Profile::Current();
    const int w = launch.editor ? launch.window.game_width : profile.screen_width;
    const int h = launch.editor ? launch.window.game_height : profile.screen_height;
    window.SetGameSize(w, h);
    std::string why;
    if (!Editor::Open(path, w, h, &why)) {
        std::cerr << "[Editor] " << path << ": " << why << std::endl;
        return GameEnd::ToMenu;
    }
    if (choose) Editor::ShowOpenList();
    if (launch.editor_tab > 0) Editor::ShowTab(launch.editor_tab);
    if (launch.look_x >= 0 && launch.look_y >= 0) Editor::LookAt(launch.look_x, launch.look_y);
    SDL_Renderer* sdl_r = window.GetSDLRenderer();
    int frames_left = launch.screenshot_after_frames;
    auto last = std::chrono::high_resolution_clock::now();
    while (true) {
        const auto started = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(started - last).count();
        last = started;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            window.TranslateEvent(event);
            Editor::HandleEvent(event);
        }
        SDL_Event down, up;
        if (ScriptedClick(down, up)) {
            Editor::HandleEvent(down);
            Editor::HandleEvent(up);
        }
        // --screenshot holds the view still, its animations at their first
        // frame.
        Editor::Update(launch.screenshot_path.empty() ? dt : 0.0f);
        const Editor::Request request = Editor::TakeRequest();
        if (request.kind == Editor::Request::Kind::Quit) return GameEnd::Quit;
        if (request.kind == Editor::Request::Kind::Exit) return GameEnd::ToMenu;
        if (request.kind == Editor::Request::Kind::Test) {
            Net::Session offline;
            Launch test = launch;
            test.direct = false;
            const GameEnd end = RunGame(window, renderer, request.setup, offline, test);
            Core::SetLocalPlayer(1);
            if (end == GameEnd::Quit) return GameEnd::Quit;
            window.SetGameSize(w, h);
            Editor::Resume(w, h);
            continue;
        }
        window.BeginFrame();
        window.Clear(0, 0, 0);
        Editor::Draw(sdl_r, renderer);
        int mx = 0, my = 0;
        window.MouseState(mx, my);
        if (const HAL::GraphicAtlas* cursors = HAL::GraphicCache::Get().GetInterfaceAtlas(51000);
            cursors != nullptr && cursors->base != nullptr && !cursors->frames.empty() &&
            launch.screenshot_path.empty()) {
            const HAL::GraphicFrame& f = cursors->frames[0];
            const SDL_Rect dest = {mx - f.hotspot_x, my - f.hotspot_y, f.rect.w, f.rect.h};
            SDL_RenderCopy(sdl_r, cursors->base, &f.rect, &dest);
        }
        if (!launch.screenshot_path.empty() && ScriptDone() && --frames_left <= 0) {
            if (window.CaptureFrame(launch.screenshot_path)) {
                std::cout << "[App] Wrote screenshot " << launch.screenshot_path << " ("
                          << window.GetWidth() << "x" << window.GetHeight() << ")" << std::endl;
            }
            return GameEnd::Quit;
        }
        window.Present();
        HoldFrame(started);
    }
}

#ifdef __ANDROID__
// A phone's app has no terminal: what the engine prints is sent to the system
// log instead, a line at a time, where `adb logcat -s OpenAoK` shows it.
void PrintToLog() {
    int ends[2];
    if (pipe(ends) != 0) return;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(ends[1], STDOUT_FILENO);
    dup2(ends[1], STDERR_FILENO);
    std::thread([fd = ends[0]] {
        std::string line;
        char chunk[512];
        ssize_t got = 0;
        while ((got = read(fd, chunk, sizeof chunk)) > 0) {
            for (ssize_t i = 0; i < got; ++i) {
                if (chunk[i] != '\n') { line += chunk[i]; continue; }
                __android_log_write(ANDROID_LOG_INFO, "OpenAoK", line.c_str());
                line.clear();
            }
        }
    }).detach();
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    if (!HAL::Platform::Start(argc, argv)) return 0;
#ifdef __ANDROID__
    PrintToLog();
    // A phone has OpenGL ES, not desktop OpenGL. The game's data was copied
    // out of the APK into the app's own storage on first run (the launcher,
    // android/app/src/main/java/org/openaok/game/CopyActivity.java), laid
    // out as the source tree is: it is played from game/build there, as from
    // build/ on a desktop, so every "../assets/..." finds its file.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    // Held sideways, either way up, as the screen the game was made for.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    // Played with the fingers as with a mouse (hal/Touch), and the back
    // gesture is Escape (hal/Window) rather than the end of the game.
    HAL::Touch::Enable();
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (const char* storage = SDL_AndroidGetInternalStoragePath()) {
        const std::string run = std::string(storage) + "/game/build";
        if (chdir(run.c_str()) != 0) SDL_Log("[App] cannot enter %s", run.c_str());
    }
#elif defined(__SWITCH__)
    // The Switch has OpenGL ES. Played with the touch screen as on a phone
    // (hal/Touch), and with the controller as a mouse and the game's keys
    // (hal/Pad); hal/Platform has already entered the game's folder.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    HAL::Touch::Enable();
    HAL::Pad::Enable();
#else
    // Desktop: the OpenGL render driver.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

    if (!LoadData()) return -1;

    Launch launch;
    launch.window.title = "OpenAoK";
    // The game's screen size: one of the three, from the profile
    // unless the command line says otherwise. Windowed, the window is this
    // size; fullscreen, this frame is scaled to the desktop.
    launch.window.game_width = Core::Profile::Current().screen_width;
    launch.window.game_height = Core::Profile::Current().screen_height;
    // Presentation defaults: fullscreen, vsync, and never faster than kFpsCap.
    launch.window.vsync = true;
    launch.window.fullscreen = true;
    ParseFlags(argc, argv, launch);
    g_script.clicks = launch.clicks;

    // A network game from the command line: the lobby first, until everyone
    // is seated.
    Net::Session session;
    if ((launch.host_port != 0 || !launch.join_address.empty()) &&
        !RunLobby(session, launch.setup, launch.host_port, launch.host_players,
                  launch.join_address)) {
        return -1;
    }

    // The headless self-check: the scenarios in src/verify/, each building its
    // own world.
    if (launch.verify_ticks > 0) {
        return Verify::Run(launch.verify_ticks);
    }

    // The menus open at their own 800x600; a game straight from the command
    // line at its own size, and the editor at its.
    HAL::Window::Config config = launch.window;
    if (!launch.direct && !launch.editor) {
        config.game_width = Screens::kMenuWidth;
        config.game_height = Screens::kMenuHeight;
    }
    HAL::Window window;
    if (!window.Init(config)) {
        std::cerr << "[App] Failed to initialize window." << std::endl;
        return -1;
    }

    // === Renderer ===
    Rendering::Renderer renderer;
    if (!renderer.Init(window)) {
        std::cerr << "[App] Failed to initialize renderer." << std::endl;
        return -1;
    }
    renderer.show_walkability = launch.show_grid;
    renderer.show_fps = launch.show_fps;

    // Sprites are read straight out of graphics.drs; without it there is
    // nothing to draw.
    if (!HAL::GraphicCache::Get().Init(window.GetSDLRenderer(),
                                       "../assets/original/Data/graphics.drs",
                                       "../assets/original/interfac_extracted/50500.bina")) {
        std::cerr << "[App] Failed to open graphics.drs." << std::endl;
        return -1;
    }

    // The game's fonts, from the CD, as LANGUAGE.DLL names them.
    Rendering::Text::Init(window.GetSDLRenderer(), "../original_game_1999/SYSTEM/FONTS");

    // The in-game interface art. Not fatal if absent -- the HUD falls back to
    // plain panels.
    HAL::GraphicCache::Get().OpenInterface("../assets/original/Data/interfac.drs");
    // Ground tiles: 21 sets of interchangeable 97x49 diamonds.
    HAL::GraphicCache::Get().OpenTerrain("../assets/original/Data/terrain.drs");
    // Where each player's colour ramp starts in the palette, from the dat.
    HAL::GraphicCache::Get().SetPlayerRamps(
        Core::ConfigManager::Get().PlayerRamps());

    // Sound: sounds.drs for the dat's sounds, the interface archive for the
    // alarms and the buttons. --screenshot runs silent.
    if (!launch.mute && launch.screenshot_path.empty()) {
        Audio::Start("../assets/original/Data/sounds.drs",
                     HAL::GraphicCache::Get().InterfaceArchive());
        HAL::Audio::SetVolume(static_cast<float>(Core::Profile::Current().sound_volume) / 10.0f);
        Audio::Music::SetVolume(Core::Profile::Current().music_volume);
    }
    Screens::Init();

    // A show of campaign slides on its own.
    if (!launch.slides.empty()) {
        RunSlides(window, ShowNamed(launch.slides), launch,
                  launch.screenshot_path.empty() ? -1 : launch.slides_at);
        return 0;
    }

    // --movie with --screenshot: one picture of the movie, as the movie
    // screen shows it.
    if (!launch.movie.empty() && !launch.screenshot_path.empty()) {
        const bool ok = Screens::Movies::DrawStill(window, launch.movie, launch.movie_frame) &&
                        window.CaptureFrame(launch.screenshot_path);
        if (ok) {
            std::cout << "[App] Wrote screenshot " << launch.screenshot_path << " ("
                      << window.GetWidth() << "x" << window.GetHeight() << ")" << std::endl;
        }
        return ok ? 0 : 1;
    }

    if (launch.editor) {
        RunEditor(window, renderer, launch, launch.editor_path, false);
    } else if (launch.direct) {
        Play play;
        play.load_path = launch.load_path;
        Save::Recording watched;
        if (!launch.play_path.empty()) {
            if (Save::ReadRecording(launch.play_path, watched)) {
                play.playback = &watched;
                Core::SetLocalPlayer(watched.viewer);
            } else {
                std::cerr << "[App] Not a recorded game: " << launch.play_path << std::endl;
            }
        }
        RunGame(window, renderer, launch.setup, session, launch, play);
    } else {
        // The menus, and the games they start, until the player quits.
        if (!launch.menu.empty()) {
            const std::string& m = launch.menu;
            if (m == "options") Screens::Open(Screens::Id::Options);
            else if (m == "history") Screens::Open(Screens::Id::History);
            else if (m == "multiplayer") Screens::Open(Screens::Id::Multiplayer);
            else if (m == "loading") Screens::Open(Screens::Id::Loading);
            else if (m == "campaigns") Screens::Open(Screens::Id::Campaigns);
            else if (m == "aftermath") {
                // No scenario on the disc has a victory or loss text, so the
                // screen shows Barbarossa's description (string 36018).
                static Scenario::File sample;
                sample.texts[Scenario::kVictory].text = Core::Strings::Get(36018);
                Screens::OpenAftermath(sample, true);
            }
            else if (m == "scenarios") Screens::Open(Screens::Id::ScenarioSelect);
            else if (m == "learn") Screens::OpenCampaignMap(8, true);
            else if (m.rfind("campaign", 0) == 0 && m.size() == 9) Screens::OpenCampaignMap(m[8] - '0', false);
            else Screens::Open(Screens::Id::MainMenu);
            if (m == "single" || m == "setup" || m == "regicide" || m == "deathmatch") {
                SDL_Event press{};
                press.type = SDL_MOUSEBUTTONDOWN;
                press.button.button = SDL_BUTTON_LEFT;
                press.button.x = 370;
                press.button.y = 100;
                Screens::HandleEvent(press);
                press.type = SDL_MOUSEBUTTONUP;
                Screens::HandleEvent(press);
                if (m != "single") {
                    const int y = m == "regicide" ? 210 : m == "deathmatch" ? 260 : 160;
                    press.type = SDL_MOUSEBUTTONDOWN;
                    press.button.x = 600;
                    press.button.y = y;
                    Screens::HandleEvent(press);
                    press.type = SDL_MOUSEBUTTONUP;
                    Screens::HandleEvent(press);
                }
            }
        }
        // The start-up movies, before the main menu and its music; not for
        // --screenshot, --clicks or --menu runs.
        bool quit = false;
        const bool scripted = !launch.screenshot_path.empty() || !launch.clicks.empty() ||
                              !launch.menu.empty();
        if (!scripted && !launch.no_movies) {
            quit = Screens::Movies::PlayStartup(window, launch.movie) ==
                   Screens::Movies::Outcome::Quit;
        }
        while (!quit) {
            Screens::Request request = RunMenus(window, launch);
            switch (request.kind) {
            case Screens::Request::Kind::Quit:
                quit = true;
                break;
            case Screens::Request::Kind::StartGame:
            case Screens::Request::Kind::LoadGame:
            case Screens::Request::Kind::PlayRecording: {
                Play play;
                Save::Recording watched;
                if (request.kind == Screens::Request::Kind::LoadGame) {
                    play.load_path = request.path;
                } else if (request.kind == Screens::Request::Kind::PlayRecording) {
                    if (!Save::ReadRecording(request.path, watched)) {
                        Screens::Open(Screens::Id::MainMenu);
                        break;
                    }
                    play.playback = &watched;
                    Core::SetLocalPlayer(watched.viewer);
                }
                GameEnd end = GameEnd::Restart;
                while (end == GameEnd::Restart) {
                    Net::Session offline;
                    end = RunGame(window, renderer, request.setup, offline, launch, play);
                    // Restarting a loaded game starts it over from its lobby.
                    if (end == GameEnd::Restart && !play.load_path.empty()) play.load_path.clear();
                }
                Core::SetLocalPlayer(1);
                quit = end == GameEnd::Quit;
                if (end == GameEnd::LoadMenu) {
                    Screens::OpenSavedGames();
                } else {
                    Screens::Open(Screens::Id::MainMenu);
                }
                break;
            }
            case Screens::Request::Kind::HostGame:
            case Screens::Request::Kind::JoinGame: {
                Net::Session net;
                Core::GameSetup setup = request.setup;
                const bool hosting = request.kind == Screens::Request::Kind::HostGame;
                int people = 2;
                if (hosting && !request.path.empty()) {
                    // Create (Restore): the saved game's own lobby, its people
                    // seated again as they join.
                    if (!Save::ReadBytes(request.path, net.restore) || !Save::ReadSetup(net.restore, setup)) {
                        std::cerr << "[App] " << Core::Strings::Get(9719) << std::endl;
                        Screens::Open(Screens::Id::MainMenu);
                        break;
                    }
                    people = 0;
                    for (int seat = 1; seat < Core::kMaxPlayers; ++seat) {
                        if (setup.players[seat].active && !setup.players[seat].computer) ++people;
                    }
                } else if (hosting) {
                    for (int seat = 2; seat < Core::kMaxPlayers; ++seat) {
                        setup.players[seat].computer = false;
                    }
                }
                if (RunLobby(net, setup, hosting ? request.port : 0, people,
                             hosting ? std::string{} : request.address)) {
                    quit = RunGame(window, renderer, setup, net, launch) == GameEnd::Quit;
                    Net::Leave(net);
                }
                Core::SetLocalPlayer(1);
                Screens::Open(Screens::Id::MainMenu);
                break;
            }
            case Screens::Request::Kind::CreateScenario:
            case Screens::Request::Kind::EditScenario: {
                const bool choose = request.kind == Screens::Request::Kind::EditScenario;
                quit = RunEditor(window, renderer, launch, std::string(), choose) == GameEnd::Quit;
                Core::SetLocalPlayer(1);
                Screens::Open(Screens::Id::MainMenu);
                break;
            }
            case Screens::Request::Kind::StartScenario: {
                // A scenario's game: the file's seats and world, at the
                // difficulty its screen chose. A campaign's scenario won
                // opens the next; either way, back to where it was chosen.
                Core::GameSetup setup = Core::DefaultSetup();
                std::string why;
                if (!Scenario::SetupFor(request.path, request.entry, setup, &why)) {
                    std::cerr << "[App] " << request.path << ": " << why << std::endl;
                    Screens::ReturnFromScenario();
                    break;
                }
                setup.difficulty = request.setup.difficulty;
                // The command line's --cheats, as for a game from its lobby.
                setup.cheats = launch.setup.cheats;
                bool won = false;
                Play play;
                play.won = &won;
                GameEnd end = GameEnd::Restart;
                while (end == GameEnd::Restart) {
                    Net::Session offline;
                    end = RunGame(window, renderer, setup, offline, launch, play);
                }
                Core::SetLocalPlayer(1);
                if (won && request.entry >= 0) {
                    Scenario::Campaign campaign;
                    if (Scenario::LoadCampaign(request.path, campaign)) {
                        Scenario::Progress::Book book = Scenario::Progress::Load();
                        Scenario::Progress::MarkWon(book, Core::Profile::Current().name,
                                                    campaign.name, request.entry,
                                                    static_cast<int>(campaign.scenarios.size()));
                        Scenario::Progress::Save(book);
                    }
                }
                quit = end == GameEnd::Quit;
                if (end == GameEnd::LoadMenu) {
                    Screens::OpenSavedGames();
                } else {
                    Screens::ReturnFromScenario();
                }
                break;
            }
            default:
                Screens::Open(Screens::Id::MainMenu);
                break;
            }
        }
    }

    Net::Leave(session);
    Audio::Stop();
    // Window destructor handles SDL cleanup
    return 0;
}
