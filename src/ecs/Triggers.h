#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"
#include "physics/Grid.h"
#include "scenario/Scenario.h"

// A scenario's triggers, run inside the tick.
//
// A trigger is on or off. While it is on, its conditions are asked; when all
// hold -- at once, for a trigger with none -- its effects are carried out in
// the file's order, and it turns itself off unless it loops. Its timers count
// the time since it was last turned on, and a looping one starts them again
// each time it fires. Triggers are asked in the file's order, so one turned on
// by an earlier trigger is asked in the same pass.
//
// They are asked once a second of game time, so a looping trigger fires at
// most once a second. Timers, selections and the rest are kept every tick.
//
// Units of the file are named by the ids the editor gave them, mapped to the
// entities the world was built with (ecs/ScenarioWorld.cpp). An effect acting
// on named units acts on those its source player owns (any owner when it names
// none); one acting on an area takes the source player's units there, through
// the effect's object, class and kind filters. Orders -- task, patrol, unload
// -- go through ecs/Orders like any player's, as the unit's owner.
//
// What is only for the screen -- instructions, sounds, the view, chat, the
// advanced buttons -- is written to a log here (Display), which the interface
// reads and the simulation never does, as ecs/Chat and ecs/Events are.
//
// Supported: conditions Bring Object to Area, Own Objects, Own Fewer Objects,
// Objects in Area, Destroy Object, Accumulate Attribute, Research Technology,
// Timer, Object Selected, AI Signal (raised through RaiseSignal), Player
// Defeated, Object Visible, Researching Technology, Units Garrisoned,
// Difficulty Level; effects Change Diplomacy, Research Technology, Send Chat,
// Play Sound, Send Tribute, Unlock and Lock Gate (kept as state: GateLocked),
// Activate and Deactivate Trigger, AI Script Goal (kept as state:
// GoalRaised), Create Object, Task Object, Declare Victory, Kill Object,
// Remove Object, Change View, Unload, Change Ownership, Patrol, Display and
// Clear Instructions, Use Advanced Buttons. Anything else is read, never true
// or never done, and named once in the log when the world is built
// (State::unsupported).
namespace ECS::Triggers {

// How often the triggers are asked.
constexpr int32_t kCheckMs = 1000;

struct Run {
    bool on = false;
    bool fired = false;          // has fired at least once
    int32_t active_ms = 0;       // since it was last turned on: its timers
};

// A unit of the scenario file, by the id its triggers name it by.
struct Placed {
    int32_t id = -1;
    entt::entity entity = entt::null;
};

struct State {
    uint64_t tick = 0;           // ticks run: what the display log is stamped with
    std::vector<Run> runs;       // one per trigger, in the file's order
    int32_t check_ms = 0;        // until the triggers are next asked
    std::vector<Placed> placed;  // sorted by id
    // AI script goals the triggers have set, per player, and the AI signals
    // raised: a bit each, 0-255.
    uint32_t goals[Core::kMaxPlayers][8] = {};
    uint32_t signals[8] = {};
    // What each player has selected (Core::SelectionCommand).
    std::vector<entt::entity> selected[Core::kMaxPlayers];
    // Gates a trigger has locked.
    std::vector<entt::entity> locked_gates;
    // The condition and effect types the file uses that are not carried out:
    // a condition's type, or 1000 plus an effect's.
    std::vector<int32_t> unsupported;
    // Players a Declare Victory has named during the pass.
    uint16_t declared = 0;
};

// What a trigger put on the screen.
enum class Show : uint8_t {
    Instructions,        // `text` in instructions line `panel`, for `seconds`
    ClearInstructions,   // line `panel` emptied
    Sound,               // `sound_id`, the dat's sound
    View,                // the view of `player` moved to tile (`x`, `y`)
    Chat,                // `text` as a chat line to `player`
    AdvancedButtons,     // the advanced command buttons switched on
};

struct Shown {
    uint64_t tick = 0;
    Show kind = Show::Instructions;
    // Whose screen: the effect's source player, 0 for everyone (the
    // instructions, which name nobody, are everyone's).
    Core::PlayerId player = 0;
    int32_t trigger = -1;
    int32_t string_id = -1;
    // Resolved: the LANGUAGE.DLL string when the effect names one, else its
    // own message (Scenario::Show).
    std::string text;
    // A narration file of Sound/scenario, without ".mp3": "j1a". Played with
    // the instruction.
    std::string sound;
    int32_t sound_id = -1;
    // How long an instruction stays, as the file says: 10 for nearly all, 0
    // for a few of the learning campaign's hints.
    int32_t seconds = 0;
    int32_t panel = 0;
    // A view's tile, in the engine's frame (tile centres on whole numbers).
    int32_t x = -1;
    int32_t y = -1;
};

struct Display {
    std::vector<Shown> lines;
};
// The lines kept; older ones are let go.
constexpr size_t kKept = 256;

// Whether a condition or effect type is carried out here.
bool ConditionSupported(int type);
bool EffectSupported(int type);

// Builds the triggers' state for `file`'s world: each trigger on or off as the
// file starts it, its units mapped to their entities. Logs the types it does
// not carry out.
void Setup(entt::registry& registry, const Scenario::File& file, std::vector<Placed> placed);

// Once a tick, before the victory module looks.
void Update(entt::registry& registry, Physics::Grid& grid, int32_t dt_ms);

// What `player` has selected now (Core::SelectionCommand).
void NoteSelection(entt::registry& registry, Core::PlayerId player,
                   const std::vector<entt::entity>& units);

// Whether the scenario has any Object Selected condition: only then need the
// interface send selections.
bool WatchesSelection(const entt::registry& registry);

// For an AI script to raise a signal an AI Signal condition waits on, and to
// learn of a goal an AI Script Goal effect set.
void RaiseSignal(entt::registry& registry, int signal);
bool GoalRaised(const entt::registry& registry, Core::PlayerId player, int goal);
// The same, cleared once read: the computer player takes each as an event
// (event-detected trigger N) the moment it thinks.
bool TakeGoal(entt::registry& registry, Core::PlayerId player, int goal);

// Whether a Lock Gate effect has locked `gate` (and no Unlock Gate since):
// for the gates' own module to keep it shut.
bool GateLocked(const entt::registry& registry, entt::entity gate);

// The entity the file's unit `id` became; null once it is gone, or if none.
entt::entity EntityOf(const entt::registry& registry, int32_t id);

// The scenario the world was built from; null for a random map game.
const Scenario::File* ScenarioOf(const entt::registry& registry);
const State* Read(const entt::registry& registry);
const Display* ReadDisplay(const entt::registry& registry);

// The objectives window: every trigger the file marks as an objective, in its
// objective order (the file's order between equals), with its text and where
// it stands -- on (pursued now), fired (done). Which of them the window
// lists, and how a done one is marked, is the interface's business.
struct Objective {
    int32_t trigger = -1;
    int32_t order = 0;
    int32_t string_id = -1;
    std::string text;            // resolved, as Shown::text
    bool on = false;
    bool fired = false;
};
std::vector<Objective> Objectives(const entt::registry& registry);

} // namespace ECS::Triggers
