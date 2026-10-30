#pragma once
#include <SDL2/SDL.h>

#include <string>
#include <vector>

#include "Screens.h"
#include "Widgets.h"
#include "scenario/Scenario.h"

// The campaign and scenario screens, the part of the screens module that
// leads into a scenario's game:
//
//   Campaigns     the book of the campaigns: each campaign's emblem and its
//                 line where interfac.drs 50344 puts them, the hovered one's
//                 description in the box at the lower left
//   CampaignMap   a campaign's map: its scenarios' markers and names where
//                 50344 + the campaign's number puts them, those opened so
//                 far; the difficulty, except for the learning campaign
//   ScenarioSelect  Single Player's Custom Scenario: the scenario files, the
//                 difficulty, OK and Cancel
//   CustomCampaigns  Single Player's Custom Campaign: the custom campaigns,
//                 the chosen one's scenarios opened so far, the difficulty
//   CampaignEditor  the Map Editor's Campaign Editor: scenarios put in a
//                 campaign file, in order
//   Mission       the scenario's instructions before its game (the "Mission
//                 Dialog"): its name, its map picture, the Instructions,
//                 History and Hints, OK
//   Aftermath     a scenario's end: its victory or loss text, then the
//                 achievements
//
// Screens.cpp owns which screen shows and the mouse; these draw and answer
// for their own.
namespace Screens::Campaigns {

using Screens::Pointer;

// The art's layouts, read from the interface archive. After the archive.
void Init();

// Each readies its screen; Screens::Open shows it.
void PrepareBook();
void PrepareMap(int number, bool from_main_menu);
void PrepareSelect();
void PrepareCustom();
void PrepareCampaignEditor();
// The custom campaigns: the campaign files that are none of the game's five.
std::vector<std::string> CustomCampaigns();
void PrepareMission(const Scenario::File& file);
// False when the scenario has no text for the end it came to.
bool PrepareAftermath(const Scenario::File& file, bool won);

// Where a scenario's game was started from, to go back to.
void Return();

void Draw(Id screen, SDL_Renderer* r, const Pointer& p);
void HandleEvent(Id screen, const SDL_Event& e, const Pointer& p, Request& request);

} // namespace Screens::Campaigns
