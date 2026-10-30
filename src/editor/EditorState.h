#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "core/Random.h"
#include "editor/Editor.h"
#include "editor/Ui.h"
#include "ecs/Simulation.h"
#include "ecs/components/Transform.h"
#include "rendering/Renderer.h"
#include "scenario/Scenario.h"
#include "screens/Widgets.h"

// The scenario editor's state, shared by its parts: the screen and its frame
// (Editor.cpp), the file and the world that shows it (Document.cpp), the
// panels (Panels.cpp, TriggerPanel.cpp).
//
// The file is kept in its own frame and the world in the grid's, a quarter
// turn from it: the world is built from the file turned, as a scenario game's
// is (ecs/ScenarioWorld's Turned). Whatever the editor takes from the world
// for the file (a click, the view, a tile painted) is turned back (FileOf,
// FileTile), and whatever of the file it shows in the world is turned
// (WorldOf, GridTile).
namespace Editor {

// The editor's modes, one per tab, in tab order.
enum Tab : int {
    kMapTab = 1,
    kTerrainTab,
    kPlayersTab,
    kUnitsTab,
    kDiplomacyTab,
    kVictoryTab,
    kOptionsTab,
    kMessagesTab,
    kCinematicsTab,
    kTriggersTab,
    kTabCount = 10,
};

// The Terrain panel's brush types: paint a terrain, an elevation, cliffs,
// copy the map, erase.
enum Brush : int { kBrushTerrain = 1, kBrushElevation, kBrushCliffs, kBrushCopy, kBrushErase };

// The Units panel's tools: Place, Delete, Move, Rotate, Select.
enum Tool : int { kPlace = 0, kDelete, kMove, kRotate, kSelect, kToolCount };

// The Units panel's lists.
enum Category : int { kUnitsList = 1, kBuildingsList, kHeroesList, kOtherList };

// A unit of the file standing in the world.
struct Ref {
    int player = 0;
    int32_t id = -1;
};

// Where every element of the editor stands on a screen of this size.
struct Frame {
    SDL_Rect tabs[kTabCount];
    SDL_Rect menu;
    SDL_Rect help;
    SDL_Rect minimap;
    bool minimap_shown = true;
    SDL_Rect panel;         // the controls of the tab
    SDL_Rect view;          // the map view
    // The Units tab's list of what to place and its four buttons.
    SDL_Rect unit_list;
    SDL_Rect unit_lists[4];
    // The Triggers tab's column.
    SDL_Rect trig_label;
    SDL_Rect trig_list;
    SDL_Rect trig_up, trig_down;
    SDL_Rect trig_buttons[3];      // Info, New, Delete
    SDL_Rect ce_label;
    SDL_Rect ce_list;
    SDL_Rect ce_up, ce_down;
    SDL_Rect ce_buttons[3];        // New Cond, New Effect, Delete
};

// What a click in the map view will set, for a trigger's field.
enum class Pick { None, Area, Location, Object, Objects, NextObject };

struct State {
    bool open = false;
    int w = 800;
    int h = 600;
    Frame frame;
    Screens::Theme theme;
    Ui ui;
    Request request;

    // The scenario, as it will be saved.
    Scenario::File doc;
    std::string path;          // where it was read from or last saved to
    bool dirty = false;

    // The world that shows it, never run, and which of its entities are the
    // file's units.
    std::unique_ptr<ECS::Simulation> sim;
    std::unordered_map<uint32_t, Ref> refs;    // by entity
    Rendering::Renderer view;  // the camera

    int tab = kMapTab;

    // Map panel.
    int map_kind = 0;          // 0 blank, 1 random, 2 seed
    int map_size = 1;          // index into the sizes offered
    int map_type = 0;          // index into the random map scripts
    int default_terrain = 0;   // index into kPaintTerrains
    std::string seed_text = "0";

    // Terrain panel.
    int brush = kBrushTerrain;
    int brush_size = 1;        // 1 Tiny ... 5 Huge
    int terrain_choice = 0;
    int terrain_scroll = 0;
    int elevation_choice = 0;
    int elevation_scroll = 0;
    int erase_choice = 0;
    int erase_scroll = 0;
    bool painting = false;
    int last_paint_x = -1;
    int last_paint_y = -1;
    // The Cliffs brush, in cells of the 3x3 cliff lattice: the cell the
    // cliff last reached, the cell the pointer was last in, and the side of
    // the last cell its cliff came in by (-1 for none).
    int cliff_last_x = -1, cliff_last_y = -1;
    int cliff_cell_x = -1, cliff_cell_y = -1;
    int cliff_back = -1;
    bool laying_cliffs = false;     // the left button down on the brush
    bool erasing_cliffs = false;    // the right
    bool cliffs_cut = false;        // a piece came out: the world laid again on release
    Core::Random cliff_rng{0x5eed};  // a straight's second drawing, half the time
    // The Map Copy brush: in Copy Mode an area of the file's tiles is
    // dragged out to copy; in Paint Mode the copy is laid where clicked,
    // turned and flipped as chosen, its units given to another player when
    // asked.
    bool copy_paint = false;
    bool choosing_area = false;
    bool has_area = false;
    int area_x0 = 0, area_y0 = 0, area_x1 = 0, area_y1 = 0;
    struct Copied {
        int w = 0, h = 0;
        std::vector<uint8_t> terrain, elevation;    // by tile, row by row
        std::vector<std::pair<int, Scenario::Unit>> units;   // player, unit from the copy's corner
    } copied;
    int copy_change = 0;       // 0 No Change Player, 1 Yes Change Player
    int copy_player = 1;

    // Units panel.
    int unit_player = 1;       // 0 Gaia
    int category = kUnitsList;
    int tool = kPlace;
    int unit_choice = 0;       // 0 is "<None>"
    int unit_scroll = 0;
    std::vector<int> unit_ids;          // what the list offers, after "<None>"
    std::vector<std::string> unit_names;
    int unit_rotation = 0;     // the next placed thing's facing (an eighth)
    entt::entity moving = entt::null;
    entt::entity selected = entt::null;
    int garrison_choice = -1, garrison_scroll = 0;   // the Garrison list's line

    // Players, Diplomacy, Options: whose settings show.
    int player = 1;
    std::string food, wood, stone, gold, tribe, name_string;
    int option_list = 0;       // Options: 0 buildings, 1 units, 2 technologies
    int disabled_choice = -1, disabled_scroll = 0;
    int full_choice = -1, full_scroll = 0;
    int test_difficulty = 2;

    // Messages.
    int message = Scenario::kInstructions;
    std::string message_string;

    // Global victory's typed figures.
    std::string relics_text, explored_text;

    // Triggers.
    int trigger = -1;          // selected
    int trigger_scroll = 0;
    int item = -1;             // selected line of the conditions and effects list
    int item_scroll = 0;
    bool trigger_info = true;  // the trigger's own fields, not a line's
    Pick pick = Pick::None;
    int pick_corner = 0;       // an area's second corner next
    std::string trigger_name, trigger_description, trigger_string, trigger_order;
    std::string line_message, line_string, line_sound, line_sound_id, line_amount, line_timer, line_panel;

    // The menu and its dialogs.
    enum class Dialog { None, Menu, SaveAs, Open, Confirm } dialog = Dialog::None;
    std::string save_name;
    std::vector<std::string> open_paths;
    std::vector<std::string> open_names;
    int open_choice = -1, open_scroll = 0;
    enum class After { None, Exit, New, Open } after = After::None;

    // A line of news across the view (saved, cannot save...).
    std::string notice;
    uint32_t notice_until = 0;
};

State& S();

// --- Document.cpp: the turn between the file and the world.
// The world's point for a point of the file, as a game of it has it.
ECS::Components::Vector2D WorldOf(const State& s, Core::Fixed32 x, Core::Fixed32 y);
// The file's point for a point of the world.
void FileOf(const State& s, float wx, float wy, float& fx, float& fy);
// The file's tile for a tile of the grid, and the grid's for one of the file's.
std::pair<int, int> FileTile(const State& s, int grid_x, int grid_y);
std::pair<int, int> GridTile(const State& s, int file_x, int file_y);

// --- Editor.cpp: the screen.
void Layout(State& s);
void Notice(State& s, const std::string& text);
// The world point under a screen point, and whether it is in the map view.
bool WorldAt(State& s, int x, int y, float& wx, float& wy);
void CentreOn(State& s, float wx, float wy);

// --- Document.cpp: the file and its world.
// A new file: eight players on a 144-tile map of grass.
Scenario::File NewFile();
// Builds the world from the file, keeping the view where it was when
// `keep_view`.
void BuildWorld(State& s, bool keep_view);
// The terrains the editor paints, in its list's order, with their names.
struct PaintTerrain {
    int dat;      // dat terrain id
    int name;     // LANGUAGE.DLL string id
};
extern const PaintTerrain kPaintTerrains[];
extern const int kPaintTerrainCount;
// The brush on a tile of the grid.
void Paint(State& s, int tile_x, int tile_y);
// Places the unit chosen at a point of the world.
void Place(State& s, float wx, float wy);
// Where Place would put the unit chosen: in the file's frame, and the point
// of the world that stands for (a building is on the tile it is in); how
// many tiles across it stands (0 for a unit or an object). False for nothing
// chosen.
bool PlaceAt(const State& s, float wx, float wy, Core::Fixed32& x, Core::Fixed32& y,
             ECS::Components::Vector2D& world, int& footprint);
// The file's thing nearest a world point, within reach; null for none.
entt::entity ThingAt(State& s, float wx, float wy);
void Delete(State& s, entt::entity e);
// The file's units inside one of the file's things, by id; and one of them
// taken out of the file.
std::vector<int32_t> Inside(const State& s, entt::entity holder);
void DeleteInside(State& s, int32_t id);
void Rotate(State& s, entt::entity e);
void MoveTo(State& s, entt::entity e, float wx, float wy);
// The Cliffs brush, on tiles of the grid: button down, pointer dragged (a
// piece laid at once in each cell of the 3x3 lattice it crosses, linked to the
// last), button up. The right button rubs cliffs out a cell at a time, the
// pieces either side ending there.
void CliffDown(State& s, int tile_x, int tile_y, bool erase);
void CliffDrag(State& s, int tile_x, int tile_y);
void CliffUp(State& s);
// The Map Copy brush: the area dragged out copied; the copy turned (0 left,
// 1 right) or flipped (2 left to right, 3 top to bottom); the copy laid with
// its middle on a file tile.
void CopyArea(State& s);
void TurnCopy(State& s, int how);
void LayCopy(State& s, int file_x, int file_y);
// The Map panel's Generate Map: blank, or a random map script's.
void Generate(State& s);
// The unit list for the player and category chosen.
void FillUnitList(State& s);
// Saves the file to `path`; `adopt` makes that the file's own place (Save,
// Save As), which a Test's copy is not.
bool SaveTo(State& s, const std::string& path, bool adopt = true);
// Players' and their seats' active flags, from the number of players.
void SetPlayerCount(State& s, int count);

// --- Panels.cpp: the tabs.
void Panel(State& s, Ui& ui);
// Fills the text fields from the file for the player or text shown.
void LoadFields(State& s);

// --- TriggerPanel.cpp.
void TriggerColumn(State& s, Ui& ui);
void TriggerPanel(State& s, Ui& ui);
// A click in the map view while a trigger's field is being picked; true
// when it was taken.
bool PickInView(State& s, float wx, float wy);

} // namespace Editor
