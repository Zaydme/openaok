#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Campaign files, and the player's progress through them.
//
// A .cpn is a table of contents and the scenarios themselves, end to end:
// "1.00", the campaign's name in 256 bytes ("cam1"), a count, then for each
// scenario its size, its offset in the file, its name (255 bytes, "Joan_scn1")
// and its file name (257, "Joan_scn1.scn"). The scenarios are ordinary
// scenario files (scenario/Scenario.h), in the order they are played. The
// game's five: cam1 Joan of Arc, cam2 Saladin, cam3 Genghis Khan, cam4
// Barbarossa, cam8 William Wallace (the learning campaign).
namespace Scenario {

struct CampaignEntry {
    std::string name;          // "Joan_scn1"
    std::string file;          // "Joan_scn1.scn"
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct Campaign {
    std::string path;
    std::string version;       // "1.00"
    std::string name;          // "cam1"
    std::vector<CampaignEntry> scenarios;
};

bool LoadCampaign(const std::string& path, Campaign& out, std::string* error = nullptr);

// The bytes of a campaign's `entry`th scenario, for Scenario::Read.
bool ReadCampaignScenario(const std::string& path, int entry, std::vector<uint8_t>& out,
                          std::string* error = nullptr);

// Where LANGUAGE.DLL names one of the game's campaigns: a block of thirty
// strings per campaign number from 34928 (cam1 at 34928 "2: Joan of Arc" ...
// cam8 at 35138 "1: William Wallace:Learning Campaign"):
//   list         the campaign list's line, numbered in the menu's order
//   scenario(i)  list + 1 + i: "1. An Unlikely Messiah"
//   name         list + 1 + the scenario count: "Joan of Arc"
//   description  list + 1000: the campaign screen's text
// All -1 for a campaign not named "camN".
struct CampaignStrings {
    int list = -1;
    int name = -1;
    int description = -1;
    int first_scenario = -1;
};
CampaignStrings StringsOf(const Campaign& campaign);
// A game campaign's number -- 1 to 8 from "cam1" to "cam8" -- or 0 for any
// other.
int CampaignNumber(const Campaign& campaign);
// The string naming the campaign's `entry`th scenario, or -1.
int ScenarioString(const Campaign& campaign, int entry);

// What the player has done in the campaigns, kept between runs.
//
// Kept per player name: each campaign's first scenario is open, and winning
// one opens the next. A text file of tab-separated lines -- player, campaign,
// scenarios open, scenarios won (a bit each) -- in SDL's preferences folder
// beside the profile.
namespace Progress {

struct Record {
    std::string player;
    std::string campaign;      // the campaign's own name: "cam1"
    int unlocked = 1;          // scenarios 0 .. unlocked - 1 may be played
    uint32_t won = 0;          // bit i: scenario i has been won
};

struct Book {
    std::vector<Record> records;
};

// campaigns.txt in the preferences folder.
std::string DefaultPath();

// A missing file is an empty book.
Book Load(const std::string& path = DefaultPath());
bool Save(const Book& book, const std::string& path = DefaultPath());

// How many of the campaign's scenarios the player may play: 1 to start with.
int Unlocked(const Book& book, const std::string& player, const std::string& campaign);
bool Playable(const Book& book, const std::string& player, const std::string& campaign, int entry);
bool Won(const Book& book, const std::string& player, const std::string& campaign, int entry);

// Records a scenario won, opening the next one of `count`.
void MarkWon(Book& book, const std::string& player, const std::string& campaign, int entry,
             int count);

} // namespace Progress

} // namespace Scenario
