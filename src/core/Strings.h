#pragma once
#include <string>

// The game's text, from LANGUAGE.DLL (assets/extracted_data/strings.json):
// every name, command and message the interface shows, by string id (19319 is
// "Ring Town Bell").
namespace Core::Strings {

bool Load(const std::string& path);

// The string, or an empty one if the DLL has no such id.
const std::string& Get(int id);

// Ids the interface names its commands by: the hotkey list's names, which
// the buttons share.
namespace Id {
constexpr int DeleteUnit = 19000;
constexpr int Garrison = 19012;
constexpr int EconomicBuildings = 19214;
constexpr int MilitaryBuildings = 19215;
constexpr int Stop = 19216;
constexpr int Cancel = 19240;
constexpr int Ungarrison = 19278;
constexpr int BoxFormation = 19303;
constexpr int LineFormation = 19304;
constexpr int StaggeredFormation = 19305;
constexpr int FlankFormation = 19306;
constexpr int Patrol = 19312;
constexpr int Guard = 19313;
constexpr int Follow = 19314;
constexpr int Aggressive = 19315;
constexpr int Defensive = 19316;
constexpr int StandGround = 19317;
constexpr int NoAttack = 19318;
constexpr int RingTownBell = 19319;
constexpr int BackToWork = 19324;
constexpr int GarrisonLabel = 20206;   // "Garrison: "
constexpr int WaitingForPlayers = 1108;
constexpr int OutOfSync = 2012;        // "Game stopped because it was out of sync!"
constexpr int Victorious = 9004;       // "You are victorious!"
constexpr int Defeated = 9005;         // "You have been defeated!"
} // namespace Id

} // namespace Core::Strings
