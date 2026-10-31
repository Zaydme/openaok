#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <entt/entt.hpp>
#include "FixedPoint.h"
// For PlayerId and Formation.
#include "Types.h"

namespace Core {

// Commands: deterministic actions ordered by a player (or a network opponent),
// queued and applied inside the tick so every machine in a lockstep game stays
// in the same state. Every command names the player who gave it; inside the
// tick that player has to own what it orders about.

// Move to a point. Destinations are continuous: routing is on the grid, but
// where a unit stops is a real position.
struct MoveCommand {
    entt::entity entity;
    Fixed32 target_x;
    Fixed32 target_y;
    PlayerId player;
};

struct GatherCommand {
    entt::entity entity;
    entt::entity target_resource;
    PlayerId player;
};

struct AttackCommand {
    entt::entity entity;
    entt::entity target_entity;
    PlayerId player;
};

// Raise a building. `unit_id` is the build button's dat unit, which the
// buildings module resolves to what actually stands. Sent at a site that is
// already there (the same unit on the same tile), it puts the villager to work
// on that site instead, at no further cost.
struct BuildCommand {
    entt::entity worker;
    int unit_id;
    int target_x;
    int target_y;
    PlayerId player;
};

// Delete one of the player's own buildings or units: the building comes down,
// the unit dies. (`building` names whichever it is.)
struct DeleteCommand {
    entt::entity building;
    PlayerId player;
};

// Queue a unit at a building. Whether it is allowed (the building is finished,
// trains that unit, has queue room, and the player can pay) is decided inside
// the tick by the production module. A building only takes orders from its
// owner.
struct TrainCommand {
    entt::entity building;
    int unit_id;
    PlayerId player;
};

// Take a unit back out of a building's queue, refunding what it cost.
struct CancelTrainCommand {
    entt::entity building;
    int slot;
    PlayerId player;
};

// Research a technology at a building. As with training, whether it may be
// queued is decided inside the tick from the player's own tech statuses.
struct ResearchCommand {
    entt::entity building;
    int tech_id;
    PlayerId player;
};

// Set where a building sends the units it trains. `target` is what was under
// the cursor, if anything -- a resource for villagers to work; the building
// itself takes its gather point away.
struct RallyCommand {
    entt::entity building;
    Fixed32 x;
    Fixed32 y;
    entt::entity target;
    PlayerId player;
};

// Move a group together: laid out in a formation about the destination,
// facing the way it marches, and keeping to its slowest member's pace.
struct FormationMoveCommand {
    std::vector<entt::entity> units;
    Fixed32 target_x;
    Fixed32 target_y;
    Formation formation;
    PlayerId player;
};

// Change how a unit fights: one of the four stances.
struct StanceCommand {
    entt::entity entity;
    uint8_t stance;   // Components::Stance
    PlayerId player;
};

// Stop whatever a unit is doing.
struct StopCommand {
    entt::entity entity;
    PlayerId player;
};

// Send a unit into one of the player's buildings, or a transport.
struct GarrisonCommand {
    entt::entity unit;
    entt::entity building;
    PlayerId player;
};

// Turn everyone out of a building.
struct UngarrisonCommand {
    entt::entity building;
    PlayerId player;
};

// Ring a Town Center's bell: villagers nearby take shelter -- or, rung again,
// go back to work.
struct TownBellCommand {
    entt::entity town_center;
    PlayerId player;
};

// Turn the villagers out of a building, each back to the job it left.
struct BackToWorkCommand {
    entt::entity building;
    PlayerId player;
};

// Standing orders for a group: patrol between where each stands and a point,
// guard a unit, or follow one.
struct PatrolCommand {
    std::vector<entt::entity> units;
    Fixed32 target_x;
    Fixed32 target_y;
    PlayerId player;
};

struct GuardCommand {
    std::vector<entt::entity> units;
    entt::entity charge;
    PlayerId player;
};

struct FollowCommand {
    std::vector<entt::entity> units;
    entt::entity charge;
    PlayerId player;
};

// Send a healer to one of its own side's wounded.
struct HealCommand {
    entt::entity unit;
    entt::entity target;
    PlayerId player;
};

// A relic errand: fetch the relic `target`, or put the relic carried in the
// monastery `target`.
struct RelicCommand {
    entt::entity unit;
    entt::entity target;
    PlayerId player;
};

// Put a transport's load ashore at the shore nearest a point: it sails
// there, and everyone aboard steps out onto the land.
struct UnloadCommand {
    entt::entity transport;
    Fixed32 target_x;
    Fixed32 target_y;
    PlayerId player;
};

// The player gives up: it is out of the game, and everything it has goes.
struct ResignCommand {
    PlayerId player;
};

// Buy or sell at one of the player's Markets: `lots` lots of 100 of a
// commodity (dat resource 0 food, 1 wood or 2 stone) for gold, each lot at
// the price of the moment (ecs/Market). `lots` is one or five.
struct MarketCommand {
    entt::entity market;
    uint8_t resource;
    uint8_t lots;
    bool buy;
    PlayerId player;
};

// Send a trade cart or cog to trade with a foreign Market or Dock: back and
// forth between it and the unit's own, carrying goods out and gold home
// (ecs/Trade).
struct TradeCommand {
    entt::entity unit;
    entt::entity partner;
    PlayerId player;
};

// Give another player a tribute: `amount` of the dat resource (0 food, 1
// wood, 2 stone, 3 gold) from the giver's stockpile, with the fee on top
// (ecs/Tribute).
struct TributeCommand {
    PlayerId recipient;
    uint8_t resource;
    int32_t amount;
    PlayerId player;
};

// Change how the player regards another: 0 ally, 1 neutral, 3 enemy
// (ecs/Diplomacy::Stance). Refused while teams are locked.
struct DiplomacyCommand {
    PlayerId other;
    uint8_t stance;
    PlayerId player;
};

// A computer player's difficulty parameter, set by its rules
// (set-difficulty-parameter), in percent: 0 ability-to-dodge-missiles, its
// units' chance of not answering a ranged attacker until struck; 1
// ability-to-maintain-distance, its ranged soldiers' chance of not backing
// off from one who sets on them.
struct DifficultyCommand {
    uint8_t parameter;
    uint8_t value;
    PlayerId player;
};

// Choose, or give up, an allied victory: winning with one's allies. Refused
// while teams are locked.
struct AlliedVictoryCommand {
    bool on;
    PlayerId player;
};

// A chat message: to everyone (audience 0), to the sender's allies (1) or to
// one player (2, `to`). Carried in lockstep, so every machine has the same
// conversation, and logged for the interface (ecs/Chat); a number from 1 to
// 30 alone is a taunt.
struct ChatCommand {
    uint8_t audience;
    PlayerId to;
    std::string text;
    PlayerId player;
};

// Signal allies: a flare at a point, which the sender's allies see on the
// minimap (ecs/Flares).
struct FlareCommand {
    Fixed32 x;
    Fixed32 y;
    PlayerId player;
};

// Lay a wall dragged from one tile to another: a foundation of `unit_id` on
// every tile of the line it can stand on, each charged its price, in order,
// until the player can pay for no more -- and the workers sent to raise them,
// each to the segment nearest it (ecs/Walls). One command for the whole line.
struct WallCommand {
    std::vector<entt::entity> workers;
    int unit_id;
    int from_x;
    int from_y;
    int to_x;
    int to_y;
    PlayerId player;
};

// Lock a gate (`lock` true) or unlock it. A locked gate opens for nobody.
struct GateCommand {
    entt::entity gate;
    bool lock;
    PlayerId player;
};

// Send villagers to mend something of their own side's: a building, a ship,
// a trebuchet. Sent at a building still going up, they carry on building it.
// The repair is paid for out of the owner's stockpile as it goes (ecs/Repairs).
struct RepairCommand {
    std::vector<entt::entity> workers;
    entt::entity target;
    PlayerId player;
};

// Pack siege engines up to move (`pack` true) or set them up to shoot: a
// trebuchet becomes its packed unit and back (ecs/Packing).
struct PackCommand {
    std::vector<entt::entity> units;
    bool pack;
    PlayerId player;
};

// A cheat code, as ecs/Cheats numbers them, typed by `player`. Refused
// unless the lobby allowed cheats.
struct CheatCommand {
    int code;
    PlayerId player;
};

// The computer players' resource cheat: the AI rules' cc-add-resource, which
// adds the given amount of a resource. Granted to a computer seat only.
struct ComputerResourceCommand {
    int resource;   // Core::ResourceType
    int amount;
    PlayerId player;
};

// What the player has selected now, for a scenario's "Object Selected"
// condition (ecs/Triggers). The interface sends it only while a scenario
// watches selections (ECS::Triggers::WatchesSelection); it travels in
// lockstep so every machine's triggers see the same selection.
struct SelectionCommand {
    std::vector<entt::entity> units;
    PlayerId player;
};

// Shift and the right button: a point added to the end of each unit's
// route -- a group's at their places in its formation. A unit standing still
// sets off for it at once (ecs/Orders).
struct WaypointCommand {
    std::vector<entt::entity> units;
    Fixed32 target_x;
    Fixed32 target_y;
    Formation formation;
    PlayerId player;
};

// Treason, in a Regicide game: done at once at one of the player's Castles
// (ecs/Spies).
struct TreasonCommand {
    entt::entity castle;
    PlayerId player;
};

// Take what a villager carries to one of the player's own buildings that
// accepts it, and stand there after.
struct DropOffCommand {
    entt::entity unit;
    entt::entity building;
    PlayerId player;
};

// A formation chosen for a group of soldiers: they form up in it where they
// stand and keep it, wherever any of them is sent (ecs/Formations).
struct FormCommand {
    std::vector<entt::entity> units;
    Formation formation;
    PlayerId player;
};

// The formations these units are in, broken up.
struct DisbandCommand {
    std::vector<entt::entity> units;
    PlayerId player;
};

using Command = std::variant<MoveCommand, GatherCommand, AttackCommand,
                             BuildCommand, DeleteCommand, TrainCommand,
                             CancelTrainCommand, ResearchCommand, RallyCommand,
                             FormationMoveCommand, StanceCommand, StopCommand,
                             GarrisonCommand, UngarrisonCommand, TownBellCommand,
                             PatrolCommand, GuardCommand, FollowCommand, HealCommand,
                             RelicCommand, UnloadCommand, ResignCommand,
                             MarketCommand, TradeCommand, TributeCommand,
                             DiplomacyCommand, AlliedVictoryCommand, ChatCommand,
                             FlareCommand, WallCommand, GateCommand, RepairCommand,
                             PackCommand, CheatCommand,
                             ComputerResourceCommand, SelectionCommand,
                             WaypointCommand, TreasonCommand, DropOffCommand,
                             BackToWorkCommand, FormCommand, DisbandCommand,
                             DifficultyCommand>;

} // namespace Core
