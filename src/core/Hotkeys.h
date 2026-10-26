#pragma once
#include <SDL2/SDL.h>
#include <string>

// The default hotkeys, from `hotkeys.json` (tools/data/extract_hotkeys.py).
//
// Every hotkey is an item of a group (the unit commands, the game commands,
// each building's own), and a key means one thing per group:
// B is the economic build menu among a villager's commands and the Barracks
// once that menu is open. So the question is always "is this key press this
// item of this group", asked by whoever owns the group.
namespace Core::Hotkeys {

enum Group : int {
    kUnitCommands = 0,
    kGameCommands = 1,
    kScrolling = 2,
    kBuild = 3,
    kTownCenter = 4,
    kDock = 5,
    kBarracks = 6,
    kArcheryRange = 7,
    kStable = 8,
    kSiegeWorkshop = 9,
    kMonastery = 10,
    kMarket = 11,
    kMilitary = 12,
    kCastle = 13,
    kGroupCount = 14,
};

// The game commands, by item number.
enum GameItem : int {
    kChat = 20,
    kSpeedUp = 21,
    kSlowDown = 22,
    kIdleVillager = 23,
    kIdleVillagerMouse = 24,
    kLastNotification = 25,
    kLastNotificationMouse = 26,
    kGoToSelected = 27,
    kGoToTownCenter = 28,
    kGoToTownCenterMouse = 29,
    kGoToMarket = 30,
    kChatBackward = 31,
    kChatForward = 32,
    kStatistics = 33,
    kGoToBarracks = 34,
    kGoToArcheryRange = 35,
    kGoToStable = 36,
    kGoToSiegeWorkshop = 37,
    kGoToDock = 38,
    kGoToMonastery = 39,
    kGoToBlacksmith = 40,
    kGoToMill = 41,
    kGoToUniversity = 42,
    kTechTree = 43,
    kGameTime = 45,
    kIdleMilitary = 46,
    kFlare = 48,
    kGoToCastle = 49,
    kGoToMiningCamp = 50,
    kGoToLumberCamp = 51,
    kMinimapCombat = 52,
    kMinimapEconomic = 53,
    kMinimapNormal = 54,
    kDiplomacy = 57,
    kObjectives = 59,
    kChatDialog = 60,
    kPause = 61,
    kSave = 62,
};

// The unit commands.
enum UnitItem : int {
    kEconomicBuildings = 0,
    kMilitaryBuildings = 1,
    kRepair = 2,
    kStop = 5,
    kUnload = 6,
    kPack = 7,
    kUnpack = 8,
    kHeal = 9,
    kConvert = 10,
    kGarrison = 11,
    kDelete = 12,
    kGatherPoint = 13,
    kAttackGround = 14,
};

// The military commands: standing orders, stances, formations.
enum MilitaryItem : int {
    kBox = 1,
    kLine = 2,
    kStaggered = 3,
    kFlank = 4,
    kPatrol = 5,
    kGuard = 6,
    kFollow = 7,
    kAggressive = 8,
    kDefensive = 9,
    kStandGround = 10,
    kNoAttack = 11,
};

// The Town Centre's own.
enum TownCenterItem : int {
    kVillager = 0,
    kRingBell = 6,
    kBackToWork = 7,
};

// The mouse wheel and middle button, bound as though they were keys.
enum MouseKey : int {
    kWheelDown = 251,
    kWheelUp = 252,
    kMiddleButton = 253,
};

bool Load(const std::string& path);

// Whether this key press is the item's binding. Ctrl and Alt must be exactly
// as bound; Shift must be held when the binding has it and is otherwise free,
// because Shift with a training key queues five, the way Shift with a click
// does.
bool Is(const SDL_KeyboardEvent& key, int group, int item);
// The same for one of the mouse's pseudo keys.
bool IsMouse(int mouse_key, int group, int item);
// The item of `group` this key press is bound to, or -1.
int ItemOf(const SDL_KeyboardEvent& key, int group);
// The item's name in LANGUAGE.DLL ("Go to Town Center"), or -1 when it has
// no default.
int NameString(int group, int item);
// The items of `group` that have a default, in the table's order; returns how
// many were written.
int Items(int group, int* out, int max_out);

// Ctrl, on a Mac keyboard either Control or Command.
bool CtrlHeld(Uint16 mod);

} // namespace Core::Hotkeys
