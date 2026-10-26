#pragma once
#include <cstdint>
#include <string_view>

// The vocabulary of the computer player language.
//
// Computer players run rule scripts (`.per` files; the standard set is in
// gamedata.drs) written in a small expert-system language, documented in
// DOCS/CPSB.DOC on the game CD. This header holds its vocabulary: every fact,
// every action, every kind of parameter, every strategic number, and the
// symbols bound to the dat's units, buildings and technologies.
//
// Nothing here runs anything. The script reader (ai/Script) compiles text
// against these tables; the interpreter (ai/Interpreter) and its bindings to
// the world (ai/Facts, ai/Actions) give the words their meaning.
namespace AI::Lang {

// What a parameter slot accepts: <value>, <rel-op>, <unit> ...
enum class Param : uint8_t {
    Value,              // an integer: literal or constant (goal, timer and taunt ids too)
    RelOp,
    Player,             // a number, a constant, a wildcard; this-any-* in actions
    Unit,               // a unit, a unit line, my-unique-unit ...
    Building,           // a building or building line (walls and gates included)
    Research,           // ri-*, an age, my-unique-unit-upgrade
    Age,
    Civ,
    Difficulty,
    MapType,
    MapSize,
    Resource,           // food wood stone gold
    Commodity,          // food wood stone
    Stance,
    StartingResources,
    StrategicNumber,
    String,
    WallType,
    DifficultyParameter,
    VictoryCondition,
    EventType,
    None,
};

enum class RelOp : uint8_t { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };

// The logical operators that combine facts.
enum class Logic : uint8_t { And, Or, Not, Nand, Nor, Xor };

// Every fact of the language.
enum class Fact : uint16_t {
    True, False,
    AttackSoldierCount, AttackWarboatCount,
    BuildingAvailable, BuildingCount, BuildingCountTotal, BuildingTypeCount, BuildingTypeCountTotal,
    CanAffordBuilding, CanAffordCompleteWall, CanAffordResearch, CanAffordUnit,
    CanBuild, CanBuildGate, CanBuildGateWithEscrow, CanBuildWall, CanBuildWallWithEscrow,
    CanBuildWithEscrow, CanBuyCommodity, CanResearch, CanResearchWithEscrow, CanSellCommodity,
    CanSpy, CanSpyWithEscrow, CanTrain, CanTrainWithEscrow,
    CcPlayersBuildingCount, CcPlayersBuildingTypeCount, CcPlayersUnitCount, CcPlayersUnitTypeCount,
    CheatsEnabled, CivSelected, CivilianPopulation, CommodityBuyingPrice, CommoditySellingPrice,
    CurrentAge, CurrentAgeTime, CurrentScore, DeathMatchGame, DefendSoldierCount, DefendWarboatCount,
    Difficulty, Doctrine, DropsiteMinDistance, EnemyBuildingsInTown, EnemyCapturedRelics,
    EscrowAmount, EventDetected, FoodAmount, GameTime, Goal, GoldAmount, HousingHeadroom,
    IdleFarmCount, MapSize, MapType, MilitaryPopulation,
    PlayerComputer, PlayerHuman, PlayerInGame, PlayerNumber, PlayerResigned, PlayerValid,
    PlayersBuildingCount, PlayersBuildingTypeCount, PlayersCiv, PlayersCivilianPopulation,
    PlayersCurrentAge, PlayersCurrentAgeTime, PlayersMilitaryPopulation, PlayersPopulation,
    PlayersScore, PlayersStance, PlayersTribute, PlayersTributeMemory, PlayersUnitCount,
    PlayersUnitTypeCount, Population, PopulationCap, PopulationHeadroom, RandomNumber,
    RegicideGame, ResearchAvailable, ResearchCompleted, ResourceFound, SharedGoal,
    SheepAndForageTooFar, SoldierCount, StanceToward, StartingAge, StartingResources,
    StoneAmount, StrategicNumber, TauntDetected, TimerTriggered, TownUnderAttack, UnitAvailable,
    UnitCount, UnitCountTotal, UnitTypeCount, UnitTypeCountTotal, VictoryCondition,
    WallCompletedPercentage, WallInvisiblePercentage, WarboatCount, WoodAmount,
    COUNT
};

// Every action of the language.
enum class Action : uint16_t {
    DoNothing, AcknowledgeEvent, AcknowledgeTaunt, AttackNow, Build, BuildForward, BuildGate,
    BuildWall, BuyCommodity, CcAddResource,
    ChatLocal, ChatLocalUsingId, ChatLocalUsingRange, ChatLocalToSelf,
    ChatToAll, ChatToAllUsingId, ChatToAllUsingRange,
    ChatToAllies, ChatToAlliesUsingId, ChatToAlliesUsingRange,
    ChatToEnemies, ChatToEnemiesUsingId, ChatToEnemiesUsingRange,
    ChatToPlayer, ChatToPlayerUsingId, ChatToPlayerUsingRange,
    ChatTrace, ClearTributeMemory, DeleteBuilding, DeleteUnit, DisableSelf, DisableTimer,
    EnableTimer, EnableWallPlacement, GenerateRandomNumber, Log, LogTrace, ReleaseEscrow,
    Research, Resign, SellCommodity, SetDifficultyParameter, SetDoctrine, SetEscrowPercentage,
    SetGoal, SetSharedGoal, SetSignal, SetStance, SetStrategicNumber, Spy, Taunt,
    TauntUsingRange, Train, TributeToPlayer,
    COUNT
};

constexpr int kMaxArgs = 4;

struct Signature {
    std::string_view name;
    Param params[kMaxArgs] = {Param::None, Param::None, Param::None, Param::None};
    int count = 0;
};

const Signature& Describe(Fact fact);
const Signature& Describe(Action action);

// By name; false for a word that is no fact (or action).
bool FindFact(std::string_view name, Fact& out);
bool FindAction(std::string_view name, Action& out);
bool FindLogic(std::string_view name, Logic& out);

// --- Parameter words ------------------------------------------------------

// Player wildcards. `any-*` in a fact is true if the fact holds for one such
// player and remembers which, in the rule variable `this-any-*`; `every-*` is
// true if it holds for all of them. In an action, `any-*` is the first such
// player and `every-*` each of them.
enum class Who : uint8_t {
    AnyAlly, AnyComputer, AnyComputerAlly, AnyComputerEnemy, AnyComputerNeutral, AnyEnemy,
    AnyHuman, AnyHumanAlly, AnyHumanEnemy, AnyHumanNeutral, AnyNeutral,
    EveryAlly, EveryComputer, EveryEnemy, EveryHuman, EveryNeutral,
    COUNT
};
constexpr int kAnyWildcards = 11;   // AnyAlly .. AnyNeutral; a rule variable each

// How a player parameter is encoded once compiled: 0-8 a seat; these above.
constexpr int kWildcardBase = 100;     // kWildcardBase + Who
constexpr int kVariableBase = 200;     // kVariableBase + Who (any-* only): this-any-*

bool FindWildcard(std::string_view name, Who& out);
bool FindVariable(std::string_view name, Who& out);
bool IsEvery(Who who);
// The `any-*` wildcard an `every-*` one has the same criteria as.
Who CriteriaOf(Who who);
// Which players a wildcard picks out: by human or computer, and by stance.
struct Criteria {
    int8_t computer = -1;   // -1 either, 0 human, 1 computer
    int8_t stance = -1;     // -1 any, else Stance
};
Criteria CriteriaFor(Who who);

enum class Stance : uint8_t { Ally, Neutral, Enemy };

// Strategic numbers, by name. The enum index is this engine's own numbering.
enum class SN : uint16_t {
    PercentCivilianExplorers, PercentCivilianBuilders, PercentCivilianGatherers,
    CapCivilianExplorers, CapCivilianBuilders, CapCivilianGatherers,
    MinimumAttackGroupSize, TotalNumberExplorers, PercentEnemySightedResponse,
    EnemySightedResponseDistance, SentryDistance, RelicReturnDistance, MinimumDefendGroupSize,
    MaximumAttackGroupSize, MaximumDefendGroupSize, MinimumPeaceLikeLevel,
    PercentExplorationRequired, ZeroPriorityDistance, MinimumCivilianExplorers,
    NumberAttackGroups, NumberDefendGroups, AttackGroupGatherSpacing, NumberExploreGroups,
    MinimumExploreGroupSize, MaximumExploreGroupSize, GoldDefendPriority, StoneDefendPriority,
    ForageDefendPriority, RelicDefendPriority, TownDefendPriority, DefenseDistance,
    NumberBoatAttackGroups, MinimumBoatAttackGroupSize, MaximumBoatAttackGroupSize,
    NumberBoatExploreGroups, MinimumBoatExploreGroupSize, MaximumBoatExploreGroupSize,
    NumberBoatDefendGroups, MinimumBoatDefendGroupSize, MaximumBoatDefendGroupSize,
    DockDefendPriority, SentryDistanceVariation, MinimumTownSize, MaximumTownSize,
    GroupCommanderSelectionMethod, ConsecutiveIdleUnitLimit, TargetEvaluationDistance,
    TargetEvaluationHitpoints, TargetEvaluationDamageCapability, TargetEvaluationKills,
    TargetEvaluationAllyProximity, TargetEvaluationRof, TargetEvaluationRandomness,
    CampMaxDistance, MillMaxDistance, TargetEvaluationAttackAttempts, TargetEvaluationRange,
    DefendOverlapDistance, ScaleMinimumAttackGroupSize, ScaleMaximumAttackGroupSize,
    AttackGroupSizeRandomness, ScalingFrequency, MaximumGaiaAttackResponse, BuildFrequency,
    AttackSeparationTimeRandomness, AttackIntelligence, InitialAttackDelay,
    SaveScenarioInformation, SpecialAttackType1, SpecialAttackInfluence1,
    MinimumWaterBodySizeForDock, NumberBuildAttemptsBeforeSkip, MaxSkipsPerAttempt,
    FoodGathererPercentage, GoldGathererPercentage, StoneGathererPercentage,
    WoodGathererPercentage, TargetEvaluationContinent, TargetEvaluationSiegeWeapon,
    GroupLeaderDefenseDistance, InitialAttackDelayType, BlotExplorationMap, BlotSize,
    IntelligentGathering, TaskUngroupedSoldiers, TargetEvaluationBoat,
    NumberEnemyObjectsRequired, NumberMaxSkipCycles, RetaskGatherAmount, MaxRetaskGatherAmount,
    MaxBuildPlanGathererPercentage, FoodDropsiteDistance, WoodDropsiteDistance,
    StoneDropsiteDistance, GoldDropsiteDistance, InitialExplorationRequired,
    RandomPlacementFactor, RequiredForestTiles, AttackDiplomacyImpact, PercentHalfExploration,
    TargetEvaluationTimeKillRatio, TargetEvaluationInProgress, AttackWinningPlayer,
    CoopShareInformation, AttackWinningPlayerFactor, CoopShareAttacking,
    CoopShareAttackingInterval, PercentageExploreExterminators, TrackPlayerHistory,
    MinimumDropsiteBuffer, UseByTypeMaxGathering, MinimumBoarHuntGroupSize,
    MinimumAmountForTrading, EasiestReactionPercentage, EasierReactionPercentage,
    HitsBeforeAllianceChange, AllowCivilianDefense, NumberForwardBuilders,
    PercentAttackSoldiers, PercentAttackBoats, DoNotScaleForDifficultyLevel, GroupFormDistance,
    IgnoreAttackGroupUnderAttack, GatherDefenseUnits, MaximumWoodDropDistance,
    MaximumFoodDropDistance, MaximumHuntDropDistance, MaximumFishBoatDropDistance,
    MaximumGoldDropDistance, MaximumStoneDropDistance,
    COUNT
};
constexpr int kStrategicNumbers = static_cast<int>(SN::COUNT);

bool FindStrategicNumber(std::string_view name, SN& out);
std::string_view NameOf(SN sn);
// A strategic number's starting value.
int DefaultOf(SN sn);

// Goals 1-40 and timers 1-10. The standard scripts use no goal id past 40,
// though the values they store reach 99.
constexpr int kGoals = 41;          // index 0 unused
constexpr int kSharedGoals = 256;
constexpr int kTimers = 11;         // index 0 unused
constexpr int kSignals = 256;
constexpr int kEvents = 256;

// --- Units, buildings and technologies --------------------------------------

// A thing a <unit>, <building> or <research-item> parameter names. Compiled
// into an index into Things(); the dat ids are checked against the dat's
// names by `Check`.
enum class ThingKind : uint8_t { Unit, Building, Research, Age, UniqueUnit, EliteUniqueUnit,
                                 UniqueUnitLine, UniqueUnitUpgrade };

constexpr int kMaxLine = 5;

struct Thing {
    std::string_view name;
    ThingKind kind = ThingKind::Unit;
    // For a unit or building: the dat units, base first. A single unit is a
    // line of one; `line` says the symbol stands for the whole line rather
    // than for its member `ids[member]`.
    int ids[kMaxLine] = {-1, -1, -1, -1, -1};
    int count = 0;
    int member = 0;
    bool line = false;
    // For a technology: the dat tech; for an age, 0 Dark .. 3 Imperial.
    int tech = -1;
};

// Every <unit>, <building> and <research-item> word, and how many.
const Thing* Things();
int ThingCount();
// Its index in Things(), or -1.
int FindThing(std::string_view name);

// The dat's name a symbol's unit or technology must carry, which `Check`
// compares against.
std::string_view ExpectedLabel(int thing);

// Simple enumerations: each word's value.
bool FindAge(std::string_view name, int& out);               // dark-age 0 .. post-imperial-age 4
bool FindCiv(std::string_view name, int& out);               // the dat's civ id
bool FindDifficulty(std::string_view name, int& out);        // hardest 0 .. easiest 4
bool FindMapType(std::string_view name, int& out);           // index into MapTypeNames
bool FindMapSize(std::string_view name, int& out);           // tiny 0 .. giant 5
bool FindResource(std::string_view name, int& out);          // Core::ResourceType
bool FindStance(std::string_view name, int& out);
bool FindStartingResources(std::string_view name, int& out); // low 0, medium 1, high 2
bool FindWallType(std::string_view name, int& out);          // a Thing index
bool FindDifficultyParameter(std::string_view name, int& out);
bool FindVictoryCondition(std::string_view name, int& out);  // standard 0 .. custom 4
bool FindEventType(std::string_view name, int& out);
bool FindRelOp(std::string_view name, RelOp& out);

bool Compare(int lhs, RelOp op, int rhs);

// The <map-type> words, in order; the symbol each defines is the word
// upper-cased with "-MAP" after it (ARABIA-MAP).
constexpr int kMapTypes = 16;
std::string_view MapTypeName(int index);

// The <civ> symbols each player's own conditional loading sees: BRITON-CIV ...
std::string_view CivSymbol(int dat_civ);

} // namespace AI::Lang
