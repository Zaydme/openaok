#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The technology tree screen's layout.
//
// The dat holds a table for the screen (assets/extracted_data/techtree.json):
// ages, buildings, units and researches, each with what leads to it. The
// table is not drawn as it stands. For the civilisation shown the layout
// takes a copy, marks what the civ cannot have, zeroes the table's layout
// hints and works them out again; then it lays the buildings out in a fixed
// order, gives each as many columns as it needs, hangs some buildings' units
// and researches in hand-picked columns and the rest where they first fit,
// stacks the towers and the walls, and sizes the age bands to what they hold.
//
// Plain data in, plain data out; no drawing and no engine state.
namespace Screens::TechTreeLayout {

// ---------------------------------------------------------------------------
// The dat's table.

// A connection: another record that leads to this one.
enum class SlotMode : int { Age = 0, Building = 1, Unit = 2, Research = 3 };
struct Slot {
    SlotMode mode = SlotMode::Age;
    int id = -1;
};

// Record status: 2 available, 3 not available to
// the civ, 4 being researched, 5 researched (or built, or buildable).
constexpr int kAvailable = 2;
constexpr int kNotAvailable = 3;
constexpr int kResearching = 4;
constexpr int kResearched = 5;

struct AgeRecord {
    int id = -1;
    int status = kAvailable;
    std::vector<int> buildings, units, techs;
    std::vector<Slot> slots;
};

struct BuildingRecord {
    int id = -1;
    int status = kAvailable;
    // 5 a building with something hanging from it, 6 one with nothing.
    int line_mode = 0;
    std::vector<int> buildings, units, techs;
    std::vector<Slot> slots;
    int location = 0;          // row in its age band, 1 or 2; worked out again
    int first[5] = {};         // columns it needs per age; worked out again
    int name_string = -1;
    int icon = -1;
    std::string name;
};

struct UnitRecord {
    int id = -1;
    int status = kAvailable;
    int line_mode = 0;         // 2 the first of its line, 3 an upgrade
    int upper_building = -1;
    int required_research = -1;
    std::vector<int> units;    // what it upgrades to
    std::vector<Slot> slots;
    int location = 0;
    int name_string = -1;
    int icon = -1;
    std::string name;
};

struct ResearchRecord {
    int id = -1;
    int status = kAvailable;
    int line_mode = 0;         // 4
    int upper_building = -1;
    std::vector<int> buildings, units, techs;
    std::vector<Slot> slots;
    int location = 0;
    int name_string = -1;
    int icon = -1;
    std::string name;
};

struct Tree {
    std::vector<AgeRecord> ages;
    std::vector<BuildingRecord> buildings;
    std::vector<UnitRecord> units;
    std::vector<ResearchRecord> researches;
    int civ = -1;
};

// Reads techtree.json. False if it could not be read.
bool Load(const std::string& path);
// The dat's table, as loaded; empty before Load.
const Tree& Master();

// ---------------------------------------------------------------------------
// Which civilisation, and what its player has.

// One command of an effect: the dat's type, A, B and D.
struct Command {
    int type = -1;
    int a = -1;
    int b = -1;
    float d = 0.0f;
};

// What the civ's tech tree takes away, from the rules: the civ's tech tree
// effect, and every technology's owning civ and effect.
struct CivRules {
    std::vector<Command> tech_tree;                  // the shown civ's
    std::vector<int> tech_civ;                       // by tech id
    std::vector<std::vector<Command>> tech_effect;   // by tech id
};

// The civ's copy of the table: statuses from its tech tree, the layout hints
// worked out again.
Tree ForCiv(int civ, const CivRules& rules);

// Before a game: everything the civ can have is shown as researched.
void PregameStatuses(Tree& tree);

// In a game, for the player's own civ: what the player has
// researched, is researching and can build.
struct PlayerView {
    int civ = -1;
    std::vector<uint8_t> tech_state;   // by tech id: 0, 2 researching, 3 researched
    std::vector<uint8_t> enabled;      // by unit id: 1 when the player may make it
};
void PlayerStatuses(Tree& tree, const PlayerView& player);

// ---------------------------------------------------------------------------
// The layout.

// The screen's figures for the size it is shown at.
struct Metrics {
    int node = 64;             // a box's width and height
    int origin_x = 490;        // where the first column's group starts: the
                               // left panel and the age column's widths
    int age_column = 150;      // the age shields' column
    float scale = 1.0f;        // height / 600, as a float
};
Metrics MetricsFor(int screen_w, int screen_h);

// What a box is.
enum class Kind : int { None = 0, Building = 1, Unit = 3, Research = 4, Through = 6 };

struct Node {
    int id = -1;
    Kind kind = Kind::None;
    int status = 0;
    int line_mode = 0;
    int location = -1;
    int age = -1;              // the age record's id
    int x = -1;
    int y = -1;
    // The line down to it from its line's box above, when it has one.
    bool has_line = false;
    int parent = -1;
    int parent_age = -1;       // index into the ages
    int parent_row = -1;
    bool first = false;        // the column's topmost box
};

struct Column {
    int building = -1;
    int building_age = -1;     // the building's age id
    int building_location = 0;
    bool box_column = false;   // the column the building's own box stands in
    bool has_children = false;
    int children[5] = {-1, -1, -1, -1, -1};
    int group = 0;             // which building, counted from 1
    int top = -1;              // the column's span, for the lines
    int bottom = -1;
    Node box;                  // the building's own box (box_column only)
    int x = 0;                 // the column's x
    Node cells[25];            // by age index * 5 + row
    int first_age = 0;         // the topmost box's cell
    int first_row = 0;
};

// A filled rectangle, both corners inclusive; corners given backwards are
// swapped when it is filled.
struct LineRect {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct Layout {
    std::vector<Column> columns;
    int bands[6] = {};         // each age band's top; bands[ages] its bottom
    int age_count = 0;
    int width = 0;             // the whole strip, from the left panel's edge
    int height = 0;
    std::vector<LineRect> lines;   // in drawing order
};

// Shown civ, and whether a player of that civ is looking (its statuses then
// come from PlayerStatuses): two special cases depend on it.
struct Viewer {
    int shown_civ = -1;
    int player_civ = -1;       // -1 before a game
    // The player's technologies, for the towers' and walls' own boxes.
    const PlayerView* player = nullptr;
};

Layout Build(const Tree& tree, const Metrics& metrics, const Viewer& viewer);

} // namespace Screens::TechTreeLayout
