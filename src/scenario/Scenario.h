#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/FixedPoint.h"
#include "core/GameSetup.h"

// Scenario files.
//
// The game's scenarios -- GOODIES/DEMO.SCN and the 31 inside the five
// campaign files -- are version "1.18" with player data 1.20 and triggers
// 1.6, and each is read here to its last byte. docs/DATA.md has the byte
// layout.
//
// A file is a short plain header -- the version, the instructions, how many
// players -- then everything else raw-deflated (zlib with no header, inflated
// by the vendored stb_image's decoder): the players' names, civilisations,
// stockpiles, ages and stances, the texts and their LANGUAGE.DLL string ids,
// the map's terrain and elevation, every unit with its id, place, facing and
// frame, the victory settings, and the triggers.
//
// Plain data. Positions are converted once, at load, from the file's floats
// to fixed point -- exactly, from the bits -- and kept in the file's own frame:
// a tile `t` spans [t, t + 1), so a unit standing in the middle of tile 10 is
// at 10.5. The engine's tiles are centred on whole numbers, so the world a
// scenario builds (ecs/ScenarioStart.cpp) stands everything half a tile back.
namespace Scenario {

// The file's per-player arrays are sixteen wide; eight players play.
constexpr int kFileSlots = 16;
constexpr int kPlayers = 8;

// A text the scenario shows, and the LANGUAGE.DLL string that stands for it.
// The campaigns name a string for nearly everything; a custom scenario names
// none (-1) and carries only its own text.
struct Text {
    std::string text;
    int32_t string_id = -1;
    // Whether this text, empty, was written as a lone NUL or as nothing (files
    // have both), so a file written back is the file read; a text with words
    // in it always ends in a NUL.
    bool nul_when_empty = false;
    friend bool operator==(const Text&, const Text&) = default;
};

// The six texts of the instructions screen and the objectives, in the file's
// order. The game's own files leave the scouts text empty.
enum TextIndex : int {
    kInstructions = 0,
    kHints,
    kVictory,
    kLoss,
    kHistory,
    kScouts,
    kTextCount
};

// The text to show: the LANGUAGE.DLL string when the scenario names one the
// DLL has, its own text otherwise. The files' own copies are English drafts
// (DEMO.SCN's "Japanese: Your armies..." is string 47018 without the
// speaker); the DLL carries the game's language.
std::string Show(const Text& text);
std::string Show(const std::string& text, int32_t string_id);

// A rectangle of tiles, corners inclusive, in the file's tile numbers: x0, y0,
// x1, y1 -- the order the trigger fields come in.
struct Area {
    int32_t x0 = -1;
    int32_t y0 = -1;
    int32_t x1 = -1;
    int32_t y1 = -1;
    bool Set() const { return x0 >= 0 && y0 >= 0 && x1 >= 0 && y1 >= 0; }
    bool Contains(int x, int y) const {
        const int lx = x0 < x1 ? x0 : x1, hx = x0 < x1 ? x1 : x0;
        const int ly = y0 < y1 ? y0 : y1, hy = y0 < y1 ? y1 : y0;
        return x >= lx && x <= hx && y >= ly && y <= hy;
    }
    friend bool operator==(const Area&, const Area&) = default;
};

struct Point {
    int32_t x = -1;
    int32_t y = -1;
    bool Set() const { return x >= 0 && y >= 0; }
    friend bool operator==(const Point&, const Point&) = default;
};

// Condition types.
namespace ConditionType {
constexpr int None = 0;
constexpr int BringObjectToArea = 1;
constexpr int BringObjectToObject = 2;
constexpr int OwnObjects = 3;
constexpr int OwnFewerObjects = 4;
constexpr int ObjectsInArea = 5;
constexpr int DestroyObject = 6;
constexpr int CaptureObject = 7;
constexpr int AccumulateAttribute = 8;
constexpr int ResearchTechnology = 9;
constexpr int Timer = 10;
constexpr int ObjectSelected = 11;
constexpr int AISignal = 12;
constexpr int PlayerDefeated = 13;
constexpr int ObjectHasTarget = 14;
constexpr int ObjectVisible = 15;
constexpr int ObjectNotVisible = 16;
constexpr int ResearchingTechnology = 17;
constexpr int UnitsGarrisoned = 18;
constexpr int DifficultyLevel = 19;
} // namespace ConditionType

namespace EffectType {
constexpr int None = 0;
constexpr int ChangeDiplomacy = 1;
constexpr int ResearchTechnology = 2;
constexpr int SendChat = 3;
constexpr int PlaySound = 4;
constexpr int SendTribute = 5;
constexpr int UnlockGate = 6;
constexpr int LockGate = 7;
constexpr int ActivateTrigger = 8;
constexpr int DeactivateTrigger = 9;
constexpr int AIScriptGoal = 10;
constexpr int CreateObject = 11;
constexpr int TaskObject = 12;
constexpr int DeclareVictory = 13;
constexpr int KillObject = 14;
constexpr int RemoveObject = 15;
constexpr int ChangeView = 16;
constexpr int Unload = 17;
constexpr int ChangeOwnership = 18;
constexpr int Patrol = 19;
constexpr int DisplayInstructions = 20;
constexpr int ClearInstructions = 21;
constexpr int FreezeUnit = 22;
constexpr int UseAdvancedButtons = 23;
} // namespace EffectType

// The name the scenario editor gives a condition or effect type, for logs.
const char* ConditionName(int type);
const char* EffectName(int type);

// What an object filter's "type" field means.
namespace ObjectKind {
constexpr int Other = 1;
constexpr int Building = 2;
constexpr int Civilian = 3;
constexpr int Military = 4;
} // namespace ObjectKind

// One condition: the sixteen fields the file carries, by their meaning. A
// field the condition does not use is -1.
struct Condition {
    int32_t type = 0;
    int32_t amount = -1;       // a count; a resource total; a difficulty level
    int32_t resource = -1;     // the dat resource, for Accumulate Attribute
    int32_t unit = -1;         // a unit of the file, by its id ("set object")
    int32_t next_unit = -1;    // a second unit of the file
    int32_t object = -1;       // a dat unit id ("object constant")
    int32_t player = -1;
    int32_t technology = -1;
    int32_t timer = -1;        // seconds
    int32_t trigger = -1;
    Area area;
    int32_t group = -1;        // a dat unit class
    int32_t kind = -1;         // ObjectKind
    int32_t ai_signal = -1;
    friend bool operator==(const Condition&, const Condition&) = default;
};

// One effect: the twenty-three fields, the message and the sound file, and
// the units of the file it names, by id.
struct Effect {
    int32_t type = 0;
    int32_t ai_goal = -1;
    int32_t amount = -1;
    int32_t resource = -1;     // the dat resource of a tribute
    int32_t diplomacy = -1;    // 0 ally, 1 neutral, 3 enemy
    int32_t selected_count = -1;
    int32_t location_unit = -1;  // a unit of the file a task is aimed at
    int32_t object = -1;       // a dat unit id: what to create, or a filter
    int32_t player = -1;       // the source player
    int32_t target_player = -1;
    int32_t technology = -1;
    int32_t string_id = -1;    // LANGUAGE.DLL, for the message
    int32_t sound_id = -1;     // the dat sound, for Play Sound
    int32_t display_time = -1; // seconds an instruction stays up
    int32_t trigger = -1;      // the trigger an effect turns on or off
    Point location;
    Area area;
    int32_t group = -1;
    int32_t kind = -1;
    int32_t panel = -1;        // which instructions line: 0, 1 or 2
    std::string message;
    std::string sound;         // a file of Sound/scenario, without .mp3
    std::vector<int32_t> units;
    // As Text::nul_when_empty: how the editor wrote these two when empty.
    bool message_nul_when_empty = false;
    bool sound_nul_when_empty = false;
    friend bool operator==(const Effect&, const Effect&) = default;
};

struct Trigger {
    bool enabled = true;       // on when the game starts
    bool looping = false;
    int32_t string_id = -1;    // LANGUAGE.DLL, for the objective's text
    bool objective = false;    // listed among the objectives
    int32_t objective_order = 0;
    int32_t start_time = 0;
    std::string description;   // the objective's own text
    std::string name;
    std::vector<Effect> effects;
    // Each list as the editor showed it: the indices in display order.
    std::vector<int32_t> effect_order;
    std::vector<Condition> conditions;
    std::vector<int32_t> condition_order;
    friend bool operator==(const Trigger&, const Trigger&) = default;
};

// One unit placed in the editor.
struct Unit {
    Core::Fixed32 x;           // the file's frame: tile t spans [t, t + 1)
    Core::Fixed32 y;
    Core::Fixed32 z;
    int32_t id = -1;           // what triggers name it by
    int32_t unit_id = -1;      // the dat unit
    uint8_t state = 0;
    // The file's rotation: radians for a unit, the shape (equal to `frame`)
    // for a tree, a rock or a mine.
    Core::Fixed32 rotation;
    int32_t frame = 0;         // the first frame: a cliff's piece, a tree's shape
    int32_t garrisoned_in = -1;  // the id of the unit holding it, or -1
    // x, y, z and rotation as the file's floats, written back while the value
    // they stand for is unchanged: a rotation of 1e-8 radians is finer than
    // fixed point holds.
    uint32_t written[4] = {0, 0, 0, 0};
    friend bool operator==(const Unit&, const Unit&) = default;
};

struct Player {
    bool active = false;
    bool human = false;
    int32_t civ = 0;
    std::string name;
    int32_t name_string = -1;
    // The stockpiles the player starts with, whole numbers.
    int32_t food = 0;
    int32_t wood = 0;
    int32_t gold = 0;
    int32_t stone = 0;
    // 0 the Dark Age to 3 the Imperial; 4 the editor's Post-Imperial (every
    // technology researched); -1 for a slot nobody uses.
    int32_t starting_age = 0;
    // How this player regards Gaia (0) and each player (1-8): 0 ally, 1
    // neutral, 3 enemy.
    uint8_t stance[Core::kMaxPlayers] = {};
    bool allied_victory = false;
    int32_t colour = 0;        // 0-7
    // The Players screen's camera, a tile's middle in the file's frame. Player
    // 1's view opens on File::camera_x/y instead, which the editor keeps
    // current; this one of player 1's is stale in the campaigns.
    Core::Fixed32 camera_x;
    Core::Fixed32 camera_y;
    std::vector<int32_t> disabled_techs;
    std::vector<int32_t> disabled_units;
    std::vector<int32_t> disabled_buildings;
    std::string ai_name;
    std::string ai_script;     // the computer player's script, when included
    uint8_t ai_type = 0;
    std::vector<Unit> units;
    friend bool operator==(const Player&, const Player&) = default;
};

// The victory settings of the Players screen's Global Victory tab.
namespace VictoryMode {
constexpr int Standard = 0;
constexpr int Conquest = 1;
constexpr int Score = 2;
constexpr int Timed = 3;
constexpr int Custom = 4;
} // namespace VictoryMode

struct Victory {
    int32_t mode = VictoryMode::Standard;
    bool conquest = true;
    int32_t relics = 0;
    int32_t explored = 0;      // percent
    bool all = false;          // every custom condition, not any one
    int32_t score = 0;
    int32_t time = 0;
    friend bool operator==(const Victory&, const Victory&) = default;
};

// The instructions screen's map picture, when the scenario carries one: 8 bits
// a pixel, rows bottom up, as a BMP stores them.
struct Bitmap {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint32_t> palette;   // 0x00RRGGBB
    std::vector<uint8_t> pixels;
    friend bool operator==(const Bitmap&, const Bitmap&) = default;
};

// What a file carries that the game does not play by -- the editor's own
// copies, the sixteen-slot tables' last eight slots, fields nobody has
// named -- kept as read so that a file written back (Write, below) is the
// file read, byte for byte once inflated. A new file's are what the scenario
// editor writes for a new scenario.
struct Kept {
    // The plain header's save type and its unused victory type.
    int32_t save_type = 2;
    int32_t victory_type = 0;
    uint32_t player_data_version_bits = 0x3F99999Au;   // 1.20f
    uint64_t trigger_version_bits = 0x3FF999999999999Aull;   // 1.6
    // The header's copy of the instructions: whether, empty, it was a NUL.
    bool header_nul_when_empty = false;
    uint8_t conquest = 1;            // the player data's first conquest flag
    // The editor's timeline: its next id, its time, its 28-byte events.
    uint16_t timeline_next = 0;
    uint32_t timeline_time = 0;
    std::string timeline_events;
    // The instructions map's four words, and its bytes as written.
    uint32_t bitmap_included = 0;
    uint32_t bitmap_width = 0;
    uint32_t bitmap_height = 0;
    int16_t bitmap_unknown = 1;
    std::string bitmap_bytes;
    uint32_t victory_unknown[3] = {0, 0, 0};
    uint32_t after_disables[2] = {0, 0};
    uint32_t unit_sets = 9;
    uint32_t settings_count = 9;
    // After the triggers: the included AI files' flag, the error table and
    // the files, as written.
    uint32_t include_ai = 1;
    uint32_t ai_errors = 0;
    std::string ai_error_table;
    std::string ai_files;

    // A slot of the sixteen-wide tables. Players 1-8 are slots 0-7, and what
    // the game plays (names, civs, stockpiles...) is theirs in Player; the
    // rest of a slot is here, and slots 8-15 are here whole.
    struct Slot {
        std::string name;              // the 256-byte field as written, tail and all
        uint32_t unknown = 4;          // unnamed; 4 in every known file
        std::string city_name;
        std::string personality_name;
        std::string ai_file;           // the AI and city files' bytes
        std::string city_file;
        // The editor's stockpiles, gold, wood, food, stone, ore, goods: for
        // players 1-8 the first four are Player's and ore and goods these.
        uint32_t stockpile[6] = {0, 0, 0, 0, 0, 0};
        // Each player's ore and goods again, as the floats after the map.
        uint32_t ore_goods_bits[2] = {0, 0};
        std::string old_victory;       // 180 words of victory conditions
        // The disable tables as written, past their counts too.
        std::vector<int32_t> techs, units, buildings;
        // Slots 8-15 only: what Player holds for the eight.
        int32_t name_string = -1;
        uint32_t enabled = 0, human = 0, civ = 0;
        std::string ai_name;
        std::string ai_script;
        uint8_t ai_type = 0;
        uint32_t allied_victory = 0;
        int32_t starting_age = -1;
        uint32_t disabled[3] = {0, 0, 0};   // the techs', units' and buildings' counts
        friend bool operator==(const Slot&, const Slot&) = default;
    };
    Slot slots[kFileSlots];

    // Each player's block after the units, besides its camera, stances and
    // colour: the editor's label ("Player 1"), two words, and the old
    // victory conditions' block from its version on. (Its second stance
    // table, the editor's own, is the stances again -- Gaia 0, itself 1, then
    // 2 ally, 3 neutral, 4 enemy -- and is written from them.)
    struct Settings {
        std::string label;
        int16_t view[2] = {0, 0};
        std::string victory;
        friend bool operator==(const Settings&, const Settings&) = default;
    };
    Settings settings[kPlayers];

    friend bool operator==(const Kept&, const Kept&) = default;
};

struct File {
    std::string version;            // "1.18"
    int32_t player_data_version = 0;  // hundredths: 120
    int32_t trigger_version = 0;    // tenths: 16
    uint32_t save_time = 0;
    std::string header_instructions;
    int32_t enabled_players = 0;
    uint32_t next_unit_id = 0;
    std::string original_name;      // the file name the editor saved it as
    Text texts[kTextCount];
    std::string pregame_cinematic;
    std::string victory_cinematic;
    std::string loss_cinematic;
    std::string background;
    Bitmap bitmap;
    // 0 is Gaia (its units only); 1-8 the players.
    Player players[Core::kMaxPlayers];
    Victory victory;
    bool all_techs = false;
    // The tile player 1's view opens on (the editor's view when saved), when
    // the player data is new enough to carry one: Joan of Arc's first opens
    // on (141, 80), where her escort stands.
    int32_t camera_x = -1;
    int32_t camera_y = -1;
    int32_t width = 0;
    int32_t height = 0;
    // By tile, row by row (index y * width + x): the dat terrain and the
    // elevation level.
    std::vector<uint8_t> terrain;
    std::vector<uint8_t> elevation;
    int8_t objectives_state = 0;
    std::vector<Trigger> triggers;
    std::vector<int32_t> trigger_order;
    Kept kept;

    // The unit of the file with that id, and its player; null if none.
    const Unit* FindUnit(int32_t id, int* player = nullptr) const;
    friend bool operator==(const File&, const File&) = default;
};

// Reads a scenario from its bytes. False, with `error` saying why, for a file
// that is not one, or one of a version this reader does not know.
bool Read(const std::vector<uint8_t>& bytes, File& out, std::string* error = nullptr);

// A lone .scn (`entry` -1), or the `entry`th scenario of a campaign file.
bool Load(const std::string& path, int entry, File& out, std::string* error = nullptr);

// The file's bytes: version 1.18, player data 1.20, triggers 1.6, laid out
// exactly as Read reads them and raw-deflated after the plain header as the
// editor's are (scenario/Writer.cpp). A file read and written comes back
// identical once inflated; what the game does not use is written from
// `kept`.
std::vector<uint8_t> Write(const File& file);
// Writes it to `path`. False, with `error` saying why, when it cannot.
bool Save(const std::string& path, const File& file, std::string* error = nullptr);
// A scenario's bytes with the part after the plain header inflated: the
// form two files are compared in, the compression being the writer's own.
bool Expand(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& out,
            std::string* error = nullptr);

// The lobby for a game of the scenario: each seat's civilisation, colour,
// name, starting age and whether the computer plays it; the map's size; the
// victory. `setup` keeps what a scenario does not decide -- the seed, the
// difficulty, the reveal setting. False when the file cannot be read.
bool SetupFor(const std::string& path, int entry, Core::GameSetup& setup,
              std::string* error = nullptr);
void SetupFor(const File& file, Core::GameSetup& setup);

} // namespace Scenario
