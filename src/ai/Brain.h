#pragma once
#include <array>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ai/Language.h"
#include "ai/Mind.h"
#include "ai/Script.h"
#include "ai/World.h"
#include "core/Command.h"
#include "core/GameSetup.h"
#include "core/Random.h"
#include "physics/Grid.h"

// The computer players.
//
// Each computer seat runs the rule scripts from gamedata.drs ("randomgame"
// and the files it loads), compiled at the start of the game under that
// player's civilisation, the lobby's difficulty, map, map size and population
// cap (ai/Script), and run a pass at a time by the rule interpreter
// (ai/Interpreter) -- facts tested against the world (ai/Facts), actions
// carried out (ai/Actions).
//
// The scripts decide what to build, train and research, when to advance and
// attack, how the villagers divide between the resources. The executor
// decides where a building goes, which villager gathers what, which soldiers
// attack and what they hit: ai/Economy, ai/Placement, ai/Military, ai/Navy,
// ai/Scouting, steered by the strategic numbers the scripts set. Every
// decision ends as an ordinary order through ecs/Orders with the same checks
// a player's get; the one exception is cc-add-resource.
//
// It runs inside the simulation's tick, deterministically, on random streams
// of its own, so in a lockstep game every machine's copy decides the same.
namespace AI {

// Soldiers or warships sent to attack, and what they are after -- across the
// water when it has to be: the transports carrying it, and how far along it
// is (marching, boarding, at sea) and for how long.
struct Group {
    std::vector<entt::entity> units;
    entt::entity target = entt::null;
    int idle_s = 0;
    std::vector<entt::entity> ships;
    int stage = 0;
    int stage_s = 0;
    // A land attack group's own (ai/Military): the soldiers it wants, its
    // task, whether attack-now made it, the passes it has spent gathering,
    // whether its task has been set going, when it was last tasked, and
    // where a random move takes it.
    int wanted = 0;
    int task = 0;
    bool now = false;
    int gathering = 0;
    bool issued = false;
    int64_t tasked_ms = 0;
    int to_x = -1, to_y = -1;
};

// A line of the computer players' chat, for whoever shows it.
struct ChatLine {
    int64_t ms = 0;
    Core::PlayerId from = 0;
    uint16_t to = 0;          // a bit per seat
    int string_id = -1;       // LANGUAGE.DLL, or -1 for `text`
    std::string text;
};

struct Brain {
    Core::PlayerId player = 0;
    Core::Random rng{1};
    int32_t think_ms = 0;

    Script::Program program;
    Mind mind;

    // The executor's memory.
    std::vector<Group> attack_groups;   // land attack groups
    // Attacks allowed: the initial attack delay is over (ai/Military).
    bool attacks_allowed = false;
    bool struck_by_player = false;   // another player has struck its things
    Group boat_attack;            // boat attack group
    std::vector<entt::entity> explorers;   // villagers sent exploring
    // Blows the player's things have taken from each ally: at
    // sn-hits-before-alliance-change, with teams not locked, it turns enemy.
    std::array<int, Core::kMaxPlayers> ally_hits{};
    // Villagers kept aside to build forward, as sn-number-forward-builders
    // asks (ai/Economy).
    std::vector<entt::entity> forward_builders;
    // Each gatherer the player sent to a resource: what it must hand in
    // there before it may be moved to another (ai/Economy), and what it had
    // handed in all told when sent (ECS::Components::Worker::delivered).
    struct Gathering {
        entt::entity villager = entt::null;
        int required = 0;
        int delivered_at = 0;
    };
    std::vector<Gathering> gathering;
    // Soldiers' explore groups past the scout's, as sn-number-explore-groups
    // asks (ai/Scouting); out of the army while they explore.
    std::vector<std::vector<entt::entity>> explore_groups;
    // Warships exploring the water, the boat attack groups' leftovers, as
    // sn-number-boat-explore-groups asks.
    std::vector<std::vector<entt::entity>> boat_explore_groups;
    // When the scout and each explore group were last sent to a point, for
    // sn-consecutive-idle-unit-limit (ai/Scouting); -2 before the first.
    int64_t scout_idle_ms = -2;
    std::vector<int64_t> explore_idle_ms;
    std::vector<int64_t> boat_explore_idle_ms;
    // A boar being hunted, and the party sent after it.
    entt::entity boar = entt::null;
    std::vector<entt::entity> hunters;
    // Where the scout looks next: a ring about home, round by bearing.
    int scout_ring = 12;
    int scout_bearing = 0;
    // The last time something of the player's in its town was struck by an
    // enemy, and by whom.
    int64_t town_struck_ms = -1;
    entt::entity intruder = entt::null;
    // The last tick of the event log read, and of the game's chat.
    uint64_t events_tick = 0;
    uint64_t chat_tick = 0;
    // The player has resigned.
    bool resigned = false;
    // The walls' rings: the tile they are about, fixed where the first Town
    // Center stood (-1 until then), and the perimeter enable-wall-placement
    // keeps other buildings clear of (0 none). See ai/Walls.
    int wall_x = -1;
    int wall_y = -1;
    int wall_perimeter = 0;
};

struct Brains {
    std::vector<Brain> brains;
    // Game time, from the first tick.
    int64_t now_ms = 0;
    // Goals every computer player shares.
    std::array<int, Lang::kSharedGoals> shared_goals{};
    // Every seat's age and when it began, for current-age-time and
    // players-current-age-time.
    std::array<int, Core::kMaxPlayers> ages{};
    std::array<int64_t, Core::kMaxPlayers> age_since{};
    // What the computer players have said, newest last.
    std::vector<ChatLine> chat;
    // Scratch for the brain thinking: the world as its rules see it.
    World world;
};

// A brain for every computer seat in the lobby, its scripts loaded.
void Setup(entt::registry& registry, const Core::GameSetup& setup);

// Once a tick: brains whose turn it is think.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

// Gives an order as the brain's player would.
void Give(entt::registry& registry, Physics::Grid& grid, const Core::Command& command);

// A scenario game: each computer seat whose player the file gives a script
// of its own plays that script instead of the random map's (the scenario
// must already be in the registry's context).
void UseScenario(entt::registry& registry, const Core::GameSetup& setup);

// The system-defined symbols a seat's scripts are loaded under: the lobby's
// difficulty, map, size, population cap, starting age and resources, victory,
// game type, team lock -- and the seat's own civilisation.
std::vector<std::string> Symbols(const Core::GameSetup& setup, Core::PlayerId seat);

// Loads the scripts for `seat`, from "randomgame" (the random map game's
// master file), reading files through `read` (by DRS id).
Script::Program Load(const Core::GameSetup& setup, Core::PlayerId seat, Core::Random& rng,
                     const std::function<bool(uint32_t, std::string&)>& read);

// The gamedata.drs id of an AI file, by the name `load` gives it; 0 if there
// is none.
uint32_t FileId(const std::string& name);

} // namespace AI
