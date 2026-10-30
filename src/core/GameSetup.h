#pragma once
#include <cstdint>
#include <string>

#include "core/Rules.h"
#include "core/Types.h"

// What the lobby decides before a game starts: the few figures the data does
// not hold (the dat's civilisation tables start every stockpile at zero and
// carry no population limit). One plain struct handed to the simulation, so a
// headless check or a network peer starts the identical game from it.
namespace Core {

enum class StartingResources : uint8_t {
    Standard,
    Low,
    Medium,
    High,
    // Death Match: every stockpile 20,000 (LANGUAGE.DLL 9752).
    DeathMatch,
};

// One seat at the table.
struct PlayerSetup {
    bool active = false;
    int civ = 1;          // dat civ id; 1 is the first playable civilisation
    TeamId team = 0;      // 0: no team
    // Played by the computer (ai/Brain) rather than a person.
    bool computer = false;
    // What the interface calls the player. Empty for a computer player, which
    // takes one of its civilisation's leaders from LANGUAGE.DLL.
    std::string name = {};
    // The player colour chosen in the lobby, 0-7; -1 for the seat's own.
    int colour = -1;
    // A scenario's own starting age for this seat (0 Dark .. 3 Imperial, 4
    // the editor's Post-Imperial); -1 for the lobby's `starting_age`.
    int starting_age = -1;
};

// Eight players besides Gaia.
constexpr int kMaxPlayers = 9;

// The map sizes: tiles across, and the label a random map script sees for
// each.
struct MapSize {
    const char* name;
    int tiles;
    const char* script_label;
};
inline constexpr MapSize kMapSizes[] = {
    {"tiny", 120, "TINY_MAP"},     {"small", 144, "SMALL_MAP"}, {"medium", 168, "MEDIUM_MAP"},
    {"normal", 200, "LARGE_MAP"},  {"large", 220, "HUGE_MAP"},  {"giant", 240, "GIGANTIC_MAP"},
};

// How a game is won: standard victory (conquest, a Wonder or the relics) or
// conquest alone.
enum class VictoryKind : uint8_t { Standard, Conquest };

struct GameSetup {
    uint32_t seed = 42;
    std::string map_script = "Arabia";
    // Tiles across, one of kMapSizes.
    int map_size = 120;
    PlayerSetup players[kMaxPlayers];
    StartingResources resources = StartingResources::Standard;
    // Selectable in steps of 25 from 25 to 200.
    int population_limit = 75;
    // The age every player starts in, 0 for the Dark Age to 3 for the
    // Imperial. Each age's technology up to it is researched at the start --
    // which technology each age is, the civ table says.
    int starting_age = 0;
    // Fog: normal, the map explored from the start, or all visible.
    Reveal reveal = Reveal::Normal;
    VictoryKind victory = VictoryKind::Standard;
    // Years a finished Wonder, or every relic held, must last to win.
    int wonder_years = 200;
    int relic_years = 200;
    // A random map game gives each player a Town Center, its villagers (as
    // many as the civ's "starting villagers" resource says) and a Scout
    // Cavalry. The scripts place none of them.
    const char* starting_town_center = "RTWC";
    const char* starting_scout = "SCOUT";
    // Regicide: every player starts with a King and a Castle besides the
    // usual start, and loses with the King.
    bool regicide = false;
    const char* regicide_king = "KINGX";
    const char* regicide_castle = "CSTL";
    // Death Match: a random map game with 20,000 of every resource
    // (`resources` DeathMatch). The game type, not only the stockpiles: the
    // computer players' scripts and the Castle's Spies read it.
    bool death_match = false;
    // The computer players' difficulty, 0 hardest to 4 easiest, in the
    // lobby list's order.
    int difficulty = 3;
    // Teams fixed for the game: no diplomacy changes.
    bool lock_teams = true;
    // Every technology open to every civilisation.
    bool all_techs = false;
    // Cheat codes may be typed into chat (LANGUAGE.DLL 13526, "Allow
    // Cheats").
    bool cheats = false;
    // The game is recorded to a file as it is played.
    bool record = false;
    // A game of a scenario rather than a random map (scenario/Scenario.h):
    // the .scn file, or the campaign file and which of its scenarios
    // (`scenario_entry`, -1 for a lone .scn). The world -- map, units,
    // stances, triggers -- is the file's; Scenario::SetupFor fills the seats.
    // Empty for a random map game.
    std::string scenario = {};
    int scenario_entry = -1;
};

// What each stockpile opens at, by the dat's resource id, for a lobby setting.
// Random map games treat Standard as Low.
inline int StartingStock(StartingResources level, int dat_resource) {
    struct Stock { int food, wood, stone, gold; };
    Stock stock{200, 200, 200, 100};
    switch (level) {
    case StartingResources::Standard:
    case StartingResources::Low:    stock = {200, 200, 200, 100}; break;
    case StartingResources::Medium: stock = {500, 500, 300, 300}; break;
    case StartingResources::High:   stock = {1000, 1000, 700, 700}; break;
    case StartingResources::DeathMatch: stock = {20000, 20000, 20000, 20000}; break;
    }
    switch (dat_resource) {
    case Rules::Res::Food:  return stock.food;
    case Rules::Res::Wood:  return stock.wood;
    case Rules::Res::Stone: return stock.stone;
    case Rules::Res::Gold:  return stock.gold;
    default:                return 0;
    }
}

// The player colours, 0-7.
constexpr int kColours = kMaxPlayers - 1;

// The colours of a game's seats as it begins. Each person keeps the one
// chosen (-1 is the seat's own). Each computer player, in seat order, takes
// the lowest that no person chose and no computer player before it took, so
// a computer never plays in a person's colour.
inline void SettleColours(GameSetup& setup) {
    bool taken[kColours] = {};
    for (int seat = 1; seat < kMaxPlayers; ++seat) {
        PlayerSetup& p = setup.players[seat];
        if (!p.active || p.computer) continue;
        if (p.colour < 0 || p.colour >= kColours) p.colour = seat - 1;
        taken[p.colour] = true;
    }
    for (int seat = 1; seat < kMaxPlayers; ++seat) {
        PlayerSetup& p = setup.players[seat];
        if (!p.active || !p.computer) continue;
        int colour = 0;
        while (colour < kColours - 1 && taken[colour]) ++colour;
        p.colour = colour;
        taken[colour] = true;
    }
}

// Two players on the default map: the engine's standing test game.
inline GameSetup DefaultSetup() {
    GameSetup setup;
    setup.players[PLAYER_GAIA] = {true, 0, 0};
    setup.players[1] = {true, 4, 0};   // Teutons
    setup.players[2] = {true, 2, 0};   // Franks
    return setup;
}

} // namespace Core
