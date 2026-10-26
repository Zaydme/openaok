#pragma once
#include <array>
#include <string>

// Figures from the AI scripts, extracted by tools/data/extract_ai.py
// (ai.json): how many villagers are kept in each age, the gatherer shares,
// and the maps that get a fishing fleet. Read by the water check
// (src/verify/WaterCheck.cpp) and loaded by main at start-up; the computer
// players themselves run the scripts (ai/Script, ai/Interpreter).
namespace AI::Doctrine {

bool Load(const std::string& path);

// How many villagers to keep in `age` (0 Dark .. 3 Imperial) under `cap`.
int VillagerTarget(int population_cap, int age);

// The resource a computer player finds itself short of, which shifts its
// gatherers toward it -- the scripts' "resource needed" goal.
enum class Need { None, Food, Wood, Gold, Stone };

// Each resource's share of the gatherers, in percent, indexed by
// Core::ResourceType (food, wood, gold, stone).
std::array<int, 4> GathererShares(int age, int civilians, Need need);

// The fishing fleet ("petersen fishboat", loaded only on the maps it names):
// whether a map is one -- by its script's name, letters only, any case --
// the smallest body of water a dock goes on, in tiles, and how many fishing
// ships to keep under `cap` in `age` (five percent of the cap in the Dark
// Age, ten later).
bool FishingMap(const std::string& script);
int DockWaterTiles();
int FishingShips(int population_cap, int age);

} // namespace AI::Doctrine
