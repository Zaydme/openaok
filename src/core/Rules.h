#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/FixedPoint.h"
#include "core/Types.h"

// The rules of the game as the data states them: every unit's numbers, every
// technology, every effect a technology applies, and each civilisation's
// starting table. Loaded once from rules.json, never changed.
//
// Every player starts from a copy of the unit rules and effects change that
// copy, so these are the numbers a player starts with, not the numbers it has
// now. See ecs/Players.
//
// Fractional figures are fixed point, converted at load to thousandths (the
// precision of the file).
namespace Core::Rules {

// Dat resource ids the engine reads by meaning, shared by costs, storage and
// effects alike. Stone is 2 and gold 3, the reverse of the engine's
// ResourceType: convert with DatResource and StockpileOf, never by casting.
namespace Res {
constexpr int Food = 0;
constexpr int Wood = 1;
constexpr int Stone = 2;
constexpr int Gold = 3;
constexpr int PopulationHeadroom = 4;
constexpr int CurrentAge = 6;
constexpr int CurrentPopulation = 11;
constexpr int TotalUnitsOwned = 19;
constexpr int FarmFoodAmount = 36;
constexpr int CivilianPopulation = 37;
constexpr int MilitaryPopulation = 40;
constexpr int StartingVillagers = 84;
constexpr int FishTrapFoodAmount = 88;
// The resources that name each age's technology in the civ table.
constexpr int CastleAgeTech = 23;
constexpr int ImperialAgeTech = 24;
constexpr int FeudalAgeTech = 25;
constexpr int DarkAgeTech = 58;
} // namespace Res

// The dat resource behind one of the four stockpiles.
constexpr int DatResource(Core::ResourceType type) {
    switch (type) {
    case Core::ResourceType::Food:  return Res::Food;
    case Core::ResourceType::Wood:  return Res::Wood;
    case Core::ResourceType::Gold:  return Res::Gold;
    case Core::ResourceType::Stone: return Res::Stone;
    default:                        return -1;
    }
}

// The stockpile a dat resource is, or COUNT if it is bookkeeping.
constexpr Core::ResourceType StockpileOf(int dat_resource) {
    switch (dat_resource) {
    case Res::Food:  return Core::ResourceType::Food;
    case Res::Wood:  return Core::ResourceType::Wood;
    case Res::Gold:  return Core::ResourceType::Gold;
    case Res::Stone: return Core::ResourceType::Stone;
    default:         return Core::ResourceType::COUNT;
    }
}

// The dat's unit types (Unit::Type): what kind of object a unit is.
namespace UnitType {
constexpr int EyeCandy = 10;
constexpr int Flag = 20;
constexpr int DeadFish = 30;
constexpr int Bird = 40;
constexpr int Combatant = 50;
constexpr int Projectile = 60;
constexpr int Creatable = 70;
constexpr int Building = 80;
constexpr int Tree = 90;
} // namespace UnitType

// The dat's classes for the siege engines that pack up to move and set up to
// shoot (the trebuchet).
namespace UnitClass {
constexpr int PackedUnit = 51;
constexpr int UnpackedSiege = 54;
} // namespace UnitClass

// An attack or armour entry: so much against so much of one armour class.
struct ClassAmount {
    int16_t cls = 0;
    int16_t amount = 0;
};

// One slot of a price. A slot that is not `paid` is a requirement checked
// without deducting: a unit's single unpaid slot of population headroom is its
// population cost.
struct Cost {
    int16_t resource = -1;
    int16_t amount = 0;
    bool paid = false;
};

// What a unit holds or contributes. Mode 0 is a stock to gather from (a
// tree's wood); the others add `amount` to the owner's resource while the unit
// exists -- 1 once and for good, 2 from when it appears, 4 from when a
// building is finished. This is how a House gives five headroom and a
// villager takes one.
struct Storage {
    int16_t resource = -1;
    Core::Fixed32 amount{};
    uint8_t mode = 0;
};

namespace StorageMode {
constexpr uint8_t Gatherable = 0;
constexpr uint8_t Permanent = 1;
constexpr uint8_t WhileAlive = 2;
constexpr uint8_t WhileStanding = 4;
} // namespace StorageMode

struct Missile {
    int8_t type = 0;
    int8_t smart = 0;
    int8_t hit_mode = 0;
    int8_t vanish_mode = 0;
    int8_t area = 0;
    Core::Fixed32 arc{};
};

struct Sounds {
    int16_t select = -1;
    int16_t dying = -1;
    int16_t train = -1;
    int16_t damage = -1;
    int16_t attack = -1;
    int16_t move = -1;
    int16_t construction = -1;
};

struct DamageGraphic {
    int16_t graphic = -1;
    int8_t percent = 0;
    int8_t apply_mode = 0;
};

// One thing a unit can do, and to what: its task list is how the dat says a
// forager gathers forage bushes and a hunter kills deer.
namespace TaskAction {
constexpr int Garrison = 3;
constexpr int Gather = 5;
constexpr int Graze = 6;          // wander about where it stands
constexpr int Combat = 7;
constexpr int Build = 101;
constexpr int Repair = 106;
constexpr int AutoConvert = 107;   // a herdable: joins whoever comes near
constexpr int Hunt = 110;
constexpr int Trade = 111;         // a trade cart or cog: goods to a foreign Market or Dock
} // namespace TaskAction

struct Task {
    int action = 0;
    int target_class = -1;   // -1: any class
    int target_unit = -1;    // -1: any unit
    int resource_in = -1;    // the dat resource it collects
    Core::Fixed32 work_range{};
    // For a gather task, how much of the worker's work rate it works this
    // target at: a fishing ship takes deep-sea fish at 1.75 of its rate and a
    // fish trap at 0.75.
    Core::Fixed32 work_value{};
    int resource_out = -1;   // what a load of it is: a fishing ship's fish is food
    // What the unit wears coming back with the load, a dat graphic: a trade
    // cart's trade task names the full cart's walk. -1 for none.
    int carrying_graphic = -1;
    // Whether the unit looks for another target of this task when its own is
    // gone (the dat's AutoSearchTargets): every villager job's gather task.
    bool auto_search = false;

    // Whether this task applies to a target of `unit_id` and `cls`.
    bool Targets(int unit_id, int cls) const {
        if (target_unit >= 0) return target_unit == unit_id;
        if (target_class >= 0) return target_class == cls;
        return false;
    }
};

// Room for every attack or armour class a unit can carry. The dat's largest
// list is five; effects can add classes a unit did not start with.
constexpr int kMaxClassEntries = 12;
constexpr int kMaxCosts = 3;
constexpr int kMaxStorage = 3;
constexpr int kMaxDamageGraphics = 4;
constexpr int kMaxTasks = 12;
constexpr int kMaxAnnexes = 4;

// A unit a building is assembled from -- a gate's post -- at an offset from
// the building's middle, in tiles, along the dat's own axes (see
// ecs/BuildingGeometry for how they lie on the engine's).
struct Annex {
    int unit = -1;
    Core::Fixed32 dx{};
    Core::Fixed32 dy{};
};

// One unit type's numbers -- everything a technology can change, and the rest
// of what the simulation plays by. Plain values only, so a player's copy of
// the whole table is a flat array.
struct UnitRules {
    bool valid = false;
    int type = 0;
    int cls = -1;
    int hp = 1;
    Core::Fixed32 los{};
    int garrison = 0;
    Core::Fixed32 size_x{};
    Core::Fixed32 size_y{};
    Core::Fixed32 speed{};
    Core::Fixed32 rotation{};
    bool enabled = false;
    // For the scenario editor: left off its unit lists, and listed among the
    // heroes rather than the units.
    bool hide_in_editor = false;
    bool hero = false;
    int dead_unit = -1;
    int dying_graphic = -1;
    int standing_graphic = -1;
    int walking_graphic = -1;
    int terrain_restriction = 0;
    // Placement beyond the restriction, as dat terrain ids (-1: none): one of
    // the grounds round the footprint must be a side terrain -- a dock's
    // beach -- and every tile under it one of the placement terrains -- a
    // dock's water or shallows.
    int placement_side_terrain[2] = {-1, -1};
    int placement_terrain[2] = {-1, -1};
    int hill_mode = 0;
    int fog_visibility = 0;
    int interaction = 0;
    int combat_level = 0;
    int minimap_mode = 0;
    int carry = 0;
    Core::Fixed32 decay{};

    Storage storage[kMaxStorage];
    int storage_count = 0;
    DamageGraphic damage_graphics[kMaxDamageGraphics];
    int damage_graphic_count = 0;
    Sounds sounds;

    Core::Fixed32 search_radius{};
    Core::Fixed32 work_rate{};

    ClassAmount attacks[kMaxClassEntries];
    int attack_count = 0;
    ClassAmount armours[kMaxClassEntries];
    int armour_count = 0;
    int base_armour = 1000;
    int terrain_defense = -1;
    Core::Fixed32 max_range{};
    Core::Fixed32 min_range{};
    Core::Fixed32 blast_width{};
    int blast_level = 0;
    Core::Fixed32 reload{};
    int projectile = -1;
    int accuracy = 0;
    Core::Fixed32 dispersion{};
    int frame_delay = 0;
    Core::Fixed32 displacement[3] = {};
    int attack_graphic = -1;
    Missile missile;

    Cost costs[kMaxCosts];
    int cost_count = 0;
    int train_time = 0;
    int train_loc = -1;
    int button = 0;
    Core::Fixed32 missiles{};
    int max_missiles = 0;
    Core::Fixed32 missile_area[3] = {};
    int secondary_projectile = -1;
    // Drawn on a building or a transport while anyone is inside: its flags.
    int garrison_graphic = -1;

    int construction_graphic = -1;
    int tech = -1;
    int head_unit = -1;
    int stack_unit = -1;
    int transform_unit = -1;
    int foundation_terrain = -1;
    int garrison_type = 0;
    Core::Fixed32 garrison_heal{};
    // 1: the buildings beside it choose which angle of its graphic it shows
    // -- a wall's runs and posts. Walls, gates and towers have it.
    int adjacent_mode = 0;
    // The ground a building claims, as a half-extent per axis of the dat's:
    // a gate is 2.0 by 0.5, four tiles one way and one the other.
    Core::Fixed32 clearance_x{};
    Core::Fixed32 clearance_y{};
    Annex annexes[kMaxAnnexes];
    int annex_count = 0;
    int icon = -1;

    Task tasks[kMaxTasks];
    int task_count = 0;
};

// What the interface calls a unit, kept apart from its numbers so a player's
// copy of the table carries no strings.
struct UnitText {
    std::string name;    // the dat's internal code, e.g. "ARCHR"
    std::string label;   // what the interface prints, e.g. "Archer"
};

constexpr int kMaxRequired = 6;

struct TechRules {
    bool valid = false;
    int required[kMaxRequired] = {};
    int required_listed = 0;
    // How many of `required` must be researched. A tech with no list and a
    // count of one can never satisfy it on its own -- it is researched only
    // when something triggers it, such as a building being finished.
    int required_count = 0;
    Cost costs[kMaxCosts];
    int cost_count = 0;
    int civ = -1;              // -1: any civilisation
    int location = -1;         // the building that researches it; -1: none
    int time = 0;              // seconds
    int effect = -1;
    int type = 0;
    int icon = -1;
    int button = 0;
};

struct TechText {
    std::string name;
    std::string label;
    std::string help;
};

// The dat's effect command types.
namespace EffectType {
constexpr int SetAttribute = 0;
constexpr int Resource = 1;
constexpr int EnableUnit = 2;
constexpr int UpgradeUnit = 3;
constexpr int AddAttribute = 4;
constexpr int MultiplyAttribute = 5;
constexpr int MultiplyResource = 6;
constexpr int TechCost = 101;
constexpr int DisableTech = 102;
constexpr int TechTime = 103;
} // namespace EffectType

// The attributes an effect can change.
namespace Attribute {
constexpr int HitPoints = 0;
constexpr int LineOfSight = 1;
constexpr int GarrisonCapacity = 2;
constexpr int SizeX = 3;
constexpr int SizeY = 4;
constexpr int Speed = 5;
constexpr int RotationSpeed = 6;
constexpr int Armour = 8;
constexpr int Attack = 9;
constexpr int ReloadTime = 10;
constexpr int Accuracy = 11;
constexpr int MaxRange = 12;
constexpr int WorkRate = 13;
constexpr int CarryCapacity = 14;
constexpr int BaseArmour = 15;
constexpr int ProjectileUnit = 16;
constexpr int GraphicsAngle = 17;
constexpr int TerrainDefense = 18;
constexpr int SmartProjectiles = 19;
constexpr int MinRange = 20;
constexpr int StorageAmount = 21;
constexpr int BlastWidth = 22;
constexpr int SearchRadius = 23;
constexpr int AllCosts = 100;
constexpr int TrainTime = 101;
constexpr int TotalMissiles = 102;
constexpr int FoodCost = 103;
constexpr int WoodCost = 104;
constexpr int GoldCost = 105;
constexpr int StoneCost = 106;
constexpr int MaxTotalMissiles = 107;
constexpr int GarrisonHealRate = 108;
} // namespace Attribute

// One command of an effect, as the dat lays it out: A, B, C and D mean
// different things per type (see ecs/Effects). D is the only fractional one.
// For attack and armour it packs a class and an amount as class * 256 +
// amount, so it is also kept as the exact integer.
struct EffectCommand {
    int type = -1;
    int a = -1;
    int b = -1;
    int c = -1;
    Core::Fixed32 d{};
    int d_int = 0;
};

struct Effect {
    bool valid = false;
    std::vector<EffectCommand> commands;
};

struct CivRules {
    int id = 0;
    std::string name;
    int tech_tree = -1;    // the effect that shapes this civ's tech tree
    int team_bonus = -1;   // the effect its allies receive
    std::vector<Core::Fixed32> resources;   // starting table, by dat resource
    std::vector<int> enabled;               // units enabled from the start
};

struct Tables {
    // Per terrain restriction, per dat terrain: thousandths of the dat's
    // figure. Above 0 a unit of that restriction may go there; above 50 a
    // building of it may stand there.
    std::vector<std::vector<int>> terrain_restrictions;
    std::vector<UnitRules> units;   // by unit id
    std::vector<UnitText> unit_text;
    std::vector<TechRules> techs;   // by tech id
    std::vector<TechText> tech_text;
    std::vector<Effect> effects;    // by effect id
    std::vector<CivRules> civs;     // by civ id; 0 is Gaia
};

// Reads rules.json. False if it could not be read.
bool Load(const std::string& path);

// Whether a unit of terrain restriction `restriction` may go on, or a building
// of it stand on, the dat terrain `terrain`. Unknown pairs are refused.
bool Passable(int restriction, int terrain);
bool Buildable(int restriction, int terrain);

// The loaded tables. Empty before Load.
const Tables& Get();

// Convenience lookups; null for an id the data does not have.
const UnitRules* Unit(int unit_id);
const TechRules* Tech(int tech_id);
const Effect* EffectById(int effect_id);
const CivRules* Civ(int civ_id);
const std::string& UnitLabel(int unit_id);
const std::string& TechLabel(int tech_id);

// The amount of one class in an attack or armour list, or `fallback` if the
// list has no entry for it.
int ClassValue(const ClassAmount* list, int count, int cls, int fallback);

// Whether these are a siege engine that packs, packed or set up. The dat makes
// them buildings by type, for the fields that type carries; they are units in
// everything else: placed, drawn and ordered as units.
bool PackingSiege(const UnitRules& rules);

// Whether these are a building in the game's sense: the dat's building type,
// less the siege engines that pack.
bool IsBuilding(const UnitRules& rules);

} // namespace Core::Rules
