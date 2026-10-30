// The scenario editor's tab panels, every control placed relative to the
// panel. The Triggers tab is in TriggerPanel.cpp.
#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "core/Rules.h"
#include "core/Strings.h"
#include "editor/EditorState.h"
#include "rendering/UIRenderer.h"
#include "screens/Screens.h"

namespace Editor {

using Core::Strings::Get;

namespace {

std::vector<std::string> Strings(std::initializer_list<int> ids) {
    std::vector<std::string> out;
    for (int id : ids) out.push_back(Get(id));
    return out;
}
std::vector<std::string> Range(int first, int last) {
    std::vector<std::string> out;
    for (int id = first; id <= last; ++id) out.push_back(Get(id));
    return out;
}

// A rect relative to the panel's origin.
struct At {
    SDL_Point o;
    SDL_Rect operator()(int x, int y, int w, int h) const { return SDL_Rect{o.x + x, o.y + y, w, h}; }
};

int Int(const std::string& s) { return static_cast<int>(std::strtol(s.c_str(), nullptr, 10)); }

// "Player 1" ... "Player 8" (10341 on), Gaia first where it is offered.
std::vector<std::string> PlayerNames(bool gaia) {
    std::vector<std::string> out;
    if (gaia) out.push_back(Get(10102));
    for (int p = 1; p <= Scenario::kPlayers; ++p) out.push_back(Get(10340 + p));
    return out;
}

// --- Map (mode 1). -----------------------------------------------------------

void MapPanel(State& s, Ui& ui, const At& at) {
    static const auto sizes = Range(10611, 10616);
    static const auto types = Range(10875, 10889);
    // A blank map's ground: the three the list offers.
    static const auto grounds = Strings({10621, 10622, 10624});
    static constexpr int kGroundTerrain[] = {0, 1, 3};   // kPaintTerrains: Grass 1, Dirt 1, Water 1
    Label(ui, at(4, 4, 120, 22), Get(10662));
    static constexpr int kKinds[] = {10652, 10653, 10654};
    for (int i = 0; i < 3; ++i) {
        Help(ui, at(4, 22 + 22 * i, 172, 22), 30515 + i);
        if (Radio(ui, at(4, 22 + 22 * i, 22, 22), s.map_kind == i)) s.map_kind = i;
        Label(ui, at(26, 22 + 22 * i, 150, 22), Get(kKinds[i]));
    }
    Label(ui, at(180, 4, 250, 22), Get(10656));
    Help(ui, at(180, 22, 250, 22), 30518);
    Drop(ui, at(180, 22, 250, 22), sizes, s.map_size);
    if (s.map_kind == 0) {
        Label(ui, at(180, 48, 250, 22), Get(10655));
        int ground = 0;
        for (int i = 0; i < 3; ++i) {
            if (kGroundTerrain[i] == s.default_terrain) ground = i;
        }
        Help(ui, at(180, 66, 250, 22), 30522);
        if (Drop(ui, at(180, 66, 250, 22), grounds, ground)) s.default_terrain = kGroundTerrain[ground];
    } else {
        Label(ui, at(180, 48, 250, 22), Get(10657));
        Help(ui, at(180, 66, 250, 22), 30519);
        Drop(ui, at(180, 66, 250, 22), types, s.map_type);
        Label(ui, at(26, 90, 150, 22), Get(s.map_kind == 2 ? 10658 : 10659));
        if (s.map_kind == 2) {
            Help(ui, at(26, 112, 60, 22), 30520);
            Field(ui, at(26, 112, 60, 22), s.seed_text, 5, true);
        } else if (Drawing(ui)) {
            Label(ui, at(26, 112, 60, 22), s.seed_text);
        }
    }
    Help(ui, at(190, 96, 220, 38), 30521);
    if (Button(ui, at(190, 96, 220, 38), Get(10660))) Generate(s);
}

// --- Terrain (mode 2). -------------------------------------------------------

void TerrainPanel(State& s, Ui& ui, const At& at) {
    static std::vector<std::string> terrains;
    if (terrains.empty()) {
        for (int i = 0; i < kPaintTerrainCount; ++i) terrains.push_back(Get(kPaintTerrains[i].name));
    }
    static std::vector<std::string> levels;
    if (levels.empty()) {
        // "Elevation 1" ... "Elevation 7" ("%s %d").
        for (int i = 1; i <= 7; ++i) levels.push_back(Get(10631) + " " + std::to_string(i));
    }
    static const auto erase = Strings({10687, 10688});
    Label(ui, at(2, 1, 115, 20), Get(10681));
    static constexpr int kTypes[] = {10010, 10631, 10682, 10685, 10686};
    static constexpr int kBrushHelp[] = {30530, 30531, 30532, 30535, 30536};
    for (int i = 0; i < 5; ++i) {
        Help(ui, at(2, 23 + 22 * i, 155, 22), kBrushHelp[i]);
        if (Radio(ui, at(2, 23 + 22 * i, 25, 22), s.brush == i + 1)) s.brush = i + 1;
        Label(ui, at(27, 23 + 22 * i, 130, 22), Get(kTypes[i]));
    }
    const bool list = s.brush == kBrushTerrain || s.brush == kBrushElevation || s.brush == kBrushErase;
    if (list) {
        Label(ui, at(158, 1, 200, 18), Get(10684));
        if (s.brush == kBrushTerrain) {
            Help(ui, at(158, 20, 200, 85), 30533);
            List(ui, at(158, 20, 200, 85), terrains, s.terrain_choice, s.terrain_scroll);
        } else if (s.brush == kBrushElevation) {
            Help(ui, at(158, 20, 200, 85), 30533);
            List(ui, at(158, 20, 200, 85), levels, s.elevation_choice, s.elevation_scroll);
        } else {
            Help(ui, at(158, 20, 200, 85), 30537);
            List(ui, at(158, 20, 200, 85), erase, s.erase_choice, s.erase_scroll);
        }
        // The brush sizes: 1, 3, 5, 7 and 9 tiles across.
        Label(ui, at(360, 1, 110, 18), Get(10671));
        for (int i = 0; i < 5; ++i) {
            Help(ui, at(363, 20 + 20 * i, 156, 20), 30525 + i);
            if (Radio(ui, at(363, 20 + 20 * i, 25, 20), s.brush_size == i + 1)) s.brush_size = i + 1;
            Label(ui, at(389, 20 + 20 * i, 130, 20), Get(10672 + i));
        }
    } else if (s.brush == kBrushCopy) {
        // Map Copy: Copy Mode drags out an area and copies it; Paint Mode
        // lays the copy, turned, flipped and given to a player as chosen.
        static const auto players = PlayerNames(true);
        const bool paint = s.copy_paint;
        Label(ui, at(158, 1, 400, 18), Get(paint ? 10696 : 10697));
        Help(ui, at(158, 20, 400, 20), 30685);
        if (Button(ui, at(158, 20, 400, 20), Get(paint ? 10691 : 10690), paint || s.has_area)) {
            if (paint) {
                s.copy_paint = false;
            } else {
                CopyArea(s);
                s.copy_paint = s.copied.w > 0;
            }
        }
        struct Turn { int x, y, label, help, how; };
        static constexpr Turn kTurns[] = {{158, 40, 10692, 30687, 0}, {358, 40, 10693, 30686, 1},
                                          {158, 60, 10694, 30688, 2}, {358, 60, 10695, 30689, 3}};
        for (const Turn& t : kTurns) {
            Help(ui, at(t.x, t.y, 200, 20), t.help);
            if (Button(ui, at(t.x, t.y, 200, 20), Get(t.label), paint)) TurnCopy(s, t.how);
        }
        Help(ui, at(158, 80, 400, 20), 30690);
        if (Button(ui, at(158, 80, 400, 20), Get(s.copy_change == 1 ? 10698 : 10700), paint)) {
            s.copy_change = 1 - s.copy_change;
        }
        if (s.copy_change == 1) {
            Label(ui, at(370, 108, 200, 18), Get(10699));
            Help(ui, at(370, 126, 150, 20), 30691);
            Drop(ui, at(370, 126, 150, 20), players, s.copy_player, paint);
        }
    }
}

// --- Players (mode 3). -------------------------------------------------------

void PlayersPanel(State& s, Ui& ui, const At& at) {
    static const auto players = PlayerNames(false);
    static const auto counts = Range(10359, 10366);
    static const auto ages = Range(4201, 4205);
    static const auto types = Strings({10211, 10212});
    static const auto civs = Range(10231, 10243);
    static const auto colours = Strings({10632, 10633, 10637, 10634, 10639, 10635, 10638, 10636});
    Scenario::Player& p = s.doc.players[s.player];
    int shown = s.player - 1;
    Help(ui, at(4, 2, 150, 20), 30538);
    if (Drop(ui, at(4, 2, 150, 20), players, shown)) {
        s.player = shown + 1;
        LoadFields(s);
    }
    Label(ui, at(4, 30, 150, 20), Get(10340));
    int count = 0;
    for (int q = 1; q <= Scenario::kPlayers; ++q) count += s.doc.players[q].active ? 1 : 0;
    int count_index = std::max(0, count - 1);
    Help(ui, at(4, 52, 150, 22), 30539);
    if (Drop(ui, at(4, 52, 150, 22), counts, count_index)) SetPlayerCount(s, count_index + 1);
    Label(ui, at(4, 78, 170, 22), Get(10261));
    int age = std::clamp(p.starting_age, 0, 4);
    Help(ui, at(4, 100, 170, 22), 30550);
    if (Drop(ui, at(4, 100, 170, 22), ages, age)) {
        p.starting_age = age;
        s.dirty = true;
    }
    // The stockpiles, two columns of two.
    struct Pile { int x, y, label, help; std::string* text; int32_t* value; };
    Pile piles[] = {{200, 22, 10251, 30540, &s.food, &p.food},
                    {200, 66, 10252, 30541, &s.wood, &p.wood},
                    {275, 22, 10253, 30543, &s.stone, &p.stone},
                    {275, 66, 10254, 30542, &s.gold, &p.gold}};
    for (Pile& pile : piles) {
        Label(ui, at(pile.x, pile.y - 20, 70, 20), Get(pile.label));
        Help(ui, at(pile.x, pile.y, 70, 20), pile.help);
        if (Field(ui, at(pile.x, pile.y, 70, 20), *pile.text, 5, true)) {
            *pile.value = Int(*pile.text);
            s.dirty = true;
        }
    }
    Label(ui, at(400, 2, 100, 20), Get(10250));
    Help(ui, at(400, 22, 125, 20), 30544);
    if (Field(ui, at(400, 22, 125, 20), s.tribe, 28)) {
        p.name = s.tribe;
        s.dirty = true;
    }
    Label(ui, at(400, 48, 150, 20), Get(10810));
    Help(ui, at(400, 68, 125, 20), 30694);
    if (Field(ui, at(400, 68, 125, 20), s.name_string, 10, true)) {
        p.name_string = s.name_string.empty() ? -1 : Int(s.name_string);
        s.dirty = true;
    }
    Label(ui, at(640, 2, 150, 20), Get(10256));
    int type = p.human ? 1 : 0;
    Help(ui, at(640, 22, 150, 20), 30545);
    if (Drop(ui, at(640, 22, 150, 20), types, type)) {
        p.human = type == 1;
        s.dirty = true;
    }
    Label(ui, at(640, 46, 150, 20), Get(10257));
    int civ = std::clamp(p.civ - 1, 0, 12);
    Help(ui, at(640, 66, 150, 20), 30546);
    if (Drop(ui, at(640, 66, 150, 20), civs, civ)) {
        p.civ = civ + 1;
        s.dirty = true;
        BuildWorld(s, true);
    }
    Label(ui, at(200, 92, 150, 20), Get(10264));
    int colour = std::clamp(p.colour, 0, 7);
    Help(ui, at(200, 110, 150, 20), 30693);
    if (Drop(ui, at(200, 110, 150, 20), colours, colour)) {
        p.colour = colour;
        s.dirty = true;
        BuildWorld(s, true);
    }
    Label(ui, at(400, 92, 200, 18), Get(10258));
    std::vector<std::string> personalities = {Get(10101)};
    if (!p.ai_name.empty()) personalities.push_back(p.ai_name);
    int personality = p.ai_name.empty() ? 0 : 1;
    Help(ui, at(400, 110, 330, 22), 30549);
    Drop(ui, at(400, 110, 330, 22), personalities, personality);
}

// --- Units (mode 4). ---------------------------------------------------------

void UnitsPanel(State& s, Ui& ui, const At& at) {
    static const auto players = PlayerNames(true);
    Help(ui, at(4, 4, 150, 22), 30560);
    if (Drop(ui, at(4, 4, 150, 22), players, s.unit_player)) FillUnitList(s);
    static constexpr int kTools[kToolCount] = {10103, 10104, 10105, 10106, 10108};
    static constexpr int kToolHelp[kToolCount] = {30556, 30555, 30557, 30558, 30554};
    for (int i = 0; i < kToolCount; ++i) {
        Help(ui, at(400, 4 + 25 * i, 175, 25), kToolHelp[i]);
        if (Radio(ui, at(400, 4 + 25 * i, 25, 25), s.tool == i)) {
            s.tool = i;
            s.selected = entt::null;
        }
        Label(ui, at(425, 4 + 25 * i, 150, 22), Get(kTools[i]));
    }
    // What the thing selected holds: the Garrison list, and Garrison Delete
    // taking the chosen one out of the scenario.
    Label(ui, at(160, 2, 230, 22), Get(10109));
    std::vector<std::string> inside;
    const std::vector<int32_t> ids = s.tool == kSelect && s.selected != entt::null ? Inside(s, s.selected)
                                                                                    : std::vector<int32_t>{};
    for (int32_t id : ids) {
        const Scenario::Unit* unit = s.doc.FindUnit(id);
        inside.push_back(unit != nullptr ? Core::Rules::UnitLabel(unit->unit_id) : std::string{});
    }
    if (s.garrison_choice >= static_cast<int>(inside.size())) s.garrison_choice = -1;
    List(ui, at(160, 22, 230, 90), inside, s.garrison_choice, s.garrison_scroll);
    if (Button(ui, at(160, 117, 20, 20), "", s.garrison_choice >= 0)) {
        DeleteInside(s, ids[static_cast<size_t>(s.garrison_choice)]);
        s.garrison_choice = -1;
    }
    Label(ui, at(180, 117, 210, 22), Get(10110));
    // The list down the left and its four buttons.
    const Frame& f = s.frame;
    Help(ui, f.unit_list, 30559);
    if (List(ui, f.unit_list, s.unit_names, s.unit_choice, s.unit_scroll)) s.tool = kPlace;
    static constexpr int kLists[4] = {10111, 10112, 10114, 10113};
    for (int i = 0; i < 4; ++i) {
        Help(ui, f.unit_lists[i], 30561 + i);
        if (Button(ui, f.unit_lists[i], Get(kLists[i]), true, s.category == i + 1)) {
            s.category = i + 1;
            s.unit_choice = 0;
            s.unit_scroll = 0;
            FillUnitList(s);
        }
    }
}

// --- Diplomacy (mode 5). -----------------------------------------------------

void DiplomacyPanel(State& s, Ui& ui, const At& at) {
    static const auto players = PlayerNames(false);
    int shown = s.player - 1;
    Help(ui, at(4, 4, 150, 22), 30625);
    if (Drop(ui, at(4, 4, 150, 22), players, shown)) s.player = shown + 1;
    Scenario::Player& p = s.doc.players[s.player];
    Label(ui, at(2, 52, 175, 22), Get(10701));
    Label(ui, at(2, 72, 175, 22), Get(10702));
    Label(ui, at(2, 92, 175, 22), Get(10703));
    Label(ui, at(2, 112, 175, 22), Get(10704));
    // One column per player, 31 apart from x 300.
    static constexpr uint8_t kStances[3] = {0, 1, 3};   // ally, neutral, enemy
    for (int q = 1; q <= Scenario::kPlayers; ++q) {
        const int x = 300 + 31 * (q - 1);
        if (Drawing(ui)) {
            const SDL_Rect cell = at(x, 28, 30, 22);
            const SDL_Color c = Rendering::UIRenderer::PlayerTextColour(static_cast<Core::PlayerId>(q));
            Screens::DrawTextIn(ui.r, ui.font, cell, std::to_string(q), c, ui.theme->text_shadow);
        }
        const bool self = q == s.player;
        for (int i = 0; i < 3; ++i) {
            const bool on = !self && p.stance[q] == kStances[i];
            Help(ui, at(x, 52 + 20 * i, 26, 18), 30627 + i);
            if (Radio(ui, at(x, 52 + 20 * i, 26, 18), on, !self)) {
                p.stance[q] = kStances[i];
                s.dirty = true;
            }
        }
        // Each player's allied victory, a box per column.
        Help(ui, at(x, 112, 26, 18), 30626);
        if (Check(ui, at(x, 112, 26, 18), s.doc.players[q].allied_victory)) s.dirty = true;
    }
}

// --- Global victory (mode 6). ------------------------------------------------

void VictoryPanel(State& s, Ui& ui, const At& at) {
    // Score: 4,000 to 14,000. Time: 300 to 1,500 years.
    static const std::vector<std::string> scores = {"14000", "13000", "12000", "11000", "10000", "9000",
                                                    "8000",  "7000",  "6000",  "5000",  "4000"};
    static constexpr int kScores[] = {14000, 13000, 12000, 11000, 10000, 9000, 8000, 7000, 6000, 5000, 4000};
    static const auto times = Range(9780, 9786);
    static constexpr int kTimes[] = {15000, 13000, 11000, 9000, 7000, 5000, 3000};
    Scenario::Victory& v = s.doc.victory;
    Label(ui, at(14, 0, 180, 18), Get(4320));
    static constexpr int kModes[] = {4332, 4321, 4330, 4329, 4331};
    for (int i = 0; i < 5; ++i) {
        Help(ui, at(14, 18 + 20 * i, 170, 18), 30650 + i);
        if (Radio(ui, at(14, 18 + 20 * i, 20, 18), v.mode == i)) {
            v.mode = i;
            s.dirty = true;
        }
        Label(ui, at(34, 18 + 20 * i, 150, 18), Get(kModes[i]));
    }
    if (v.mode == Scenario::VictoryMode::Score) {
        Label(ui, at(190, 18, 130, 18), Get(4330));
        int i = 0;
        for (int k = 0; k < 11; ++k) {
            if (kScores[k] == v.score) i = k;
        }
        Help(ui, at(190, 40, 130, 22), 30655);
        if (Drop(ui, at(190, 40, 130, 22), scores, i)) {
            v.score = kScores[i];
            s.dirty = true;
        }
    } else if (v.mode == Scenario::VictoryMode::Timed) {
        Label(ui, at(190, 18, 220, 18), Get(4329));
        int i = 0;
        for (int k = 0; k < 7; ++k) {
            if (kTimes[k] == v.time) i = k;
        }
        Help(ui, at(190, 40, 220, 22), 30656);
        if (Drop(ui, at(190, 40, 220, 22), times, i)) {
            v.time = kTimes[i];
            s.dirty = true;
        }
    } else if (v.mode == Scenario::VictoryMode::Custom) {
        bool explore = v.explored > 0, relics = v.relics > 0;
        Help(ui, at(190, 18, 170, 18), 30568);
        if (Check(ui, at(190, 18, 20, 18), v.conquest)) s.dirty = true;
        Label(ui, at(210, 18, 150, 18), Get(4321));
        Help(ui, at(190, 38, 170, 18), 30570);
        if (Check(ui, at(190, 38, 20, 18), explore)) {
            v.explored = explore ? std::max(1, Int(s.explored_text)) : 0;
            s.dirty = true;
        }
        Label(ui, at(210, 38, 150, 18), Get(4322));
        Help(ui, at(190, 58, 170, 18), 30572);
        if (Check(ui, at(190, 58, 20, 18), relics)) {
            v.relics = relics ? std::max(1, Int(s.relics_text)) : 0;
            s.dirty = true;
        }
        Label(ui, at(210, 58, 150, 18), Get(4324));
        Label(ui, at(370, 0, 120, 18), Get(10380));
        Help(ui, at(370, 38, 50, 18), 30575);
        if (Field(ui, at(370, 38, 50, 18), s.explored_text, 3, true) && explore) {
            v.explored = std::clamp(Int(s.explored_text), 0, 100);
            s.dirty = true;
        }
        Help(ui, at(370, 58, 50, 18), 30574);
        if (Field(ui, at(370, 58, 50, 18), s.relics_text, 2, true) && relics) {
            v.relics = std::max(0, Int(s.relics_text));
            s.dirty = true;
        }
        Help(ui, at(430, 18, 140, 18), 30565);
        if (Radio(ui, at(430, 18, 20, 18), !v.all)) v.all = false;
        Label(ui, at(450, 18, 120, 18), Get(10378));
        Help(ui, at(430, 38, 140, 18), 30567);
        if (Radio(ui, at(430, 38, 20, 18), v.all)) v.all = true;
        Label(ui, at(450, 38, 120, 18), Get(10379));
    }
}

// --- Options (mode 7). -------------------------------------------------------

// What the Options tab's lists hold: a player's disabled buildings, units or
// technologies, and what else there is to disable.
void OptionLists(const State& s, std::vector<int>& disabled, std::vector<int>& full,
                 std::vector<std::string>& disabled_names, std::vector<std::string>& full_names) {
    const Scenario::Player& p = s.doc.players[s.player];
    const auto& rules = Core::Rules::Get();
    const std::vector<int32_t>& list = s.option_list == 0 ? p.disabled_buildings
                                     : s.option_list == 1 ? p.disabled_units : p.disabled_techs;
    disabled.assign(list.begin(), list.end());
    auto label = [&](int id) -> std::string {
        const std::string& l = s.option_list == 2 ? Core::Rules::TechLabel(id) : Core::Rules::UnitLabel(id);
        return l.empty() ? std::to_string(id) : l;
    };
    for (int id : disabled) disabled_names.push_back(label(id));
    // One line a name: a building's later ages are the same building.
    auto named = [&](const std::string& name) {
        return std::find(full_names.begin(), full_names.end(), name) != full_names.end() ||
               std::find(disabled_names.begin(), disabled_names.end(), name) != disabled_names.end();
    };
    if (s.option_list == 2) {
        for (int id = 0; id < static_cast<int>(rules.techs.size()); ++id) {
            const auto& t = rules.techs[static_cast<size_t>(id)];
            if (!t.valid || t.location < 0 || Core::Rules::TechLabel(id).empty()) continue;
            if (std::find(disabled.begin(), disabled.end(), id) != disabled.end() || named(label(id))) continue;
            full.push_back(id);
            full_names.push_back(label(id));
        }
    } else {
        const int type = s.option_list == 0 ? 80 : 70;
        for (int id = 0; id < static_cast<int>(rules.units.size()); ++id) {
            const auto& u = rules.units[static_cast<size_t>(id)];
            if (!u.valid || u.type != type || u.train_loc < 0 || Core::Rules::UnitLabel(id).empty()) continue;
            if (std::find(disabled.begin(), disabled.end(), id) != disabled.end() || named(label(id))) continue;
            full.push_back(id);
            full_names.push_back(label(id));
        }
    }
}

void OptionsPanel(State& s, Ui& ui, const At& at) {
    static const auto players = PlayerNames(false);
    static const auto difficulties = Range(11216, 11220);
    int shown = s.player - 1;
    Help(ui, at(4, 2, 150, 20), 30607);
    if (Drop(ui, at(4, 2, 150, 20), players, shown)) {
        s.player = shown + 1;
        s.disabled_choice = s.full_choice = -1;
    }
    Help(ui, at(4, 30, 170, 20), 30605);
    if (Check(ui, at(4, 30, 20, 20), s.doc.all_techs)) s.dirty = true;
    Label(ui, at(24, 30, 150, 20), Get(9732));
    Label(ui, at(4, 53, 150, 20), Get(13018));
    Help(ui, at(4, 71, 150, 22), 30701);
    Drop(ui, at(4, 71, 150, 22), difficulties, s.test_difficulty);
    Scenario::Player& p = s.doc.players[s.player];
    Help(ui, at(4, 103, 150, 22), 30702);
    if (Button(ui, at(4, 103, 150, 22), Get(13030))) {
        // The player's view: where the map view looks now.
        float wx = 0, wy = 0;
        const SDL_Rect& v = s.frame.view;
        if (WorldAt(s, v.x + v.w / 2, v.y + v.h / 2, wx, wy)) {
            // The middle of the file's tile under it.
            const auto [fx, fy] = FileTile(s, static_cast<int>(wx + 0.5f), static_cast<int>(wy + 0.5f));
            p.camera_x = Core::Fixed32(fx) + Core::Fixed32(1) / Core::Fixed32(2);
            p.camera_y = Core::Fixed32(fy) + Core::Fixed32(1) / Core::Fixed32(2);
            if (s.player == 1) {
                s.doc.camera_x = fx;
                s.doc.camera_y = fy;
            }
            s.dirty = true;
        }
    }
    Help(ui, at(4, 125, 150, 22), 30703);
    if (Button(ui, at(4, 125, 150, 22), Get(13031))) {
        const auto world = WorldOf(s, p.camera_x, p.camera_y);
        CentreOn(s, world.x.ToFloat(), world.y.ToFloat());
    }
    static constexpr int kKinds[] = {10112, 10111, 10319};
    for (int i = 0; i < 3; ++i) {
        const int x = 260 + (i == 0 ? 0 : i == 1 ? 180 : 350);
        Help(ui, at(x, 2, 170, 20), 30704 + i);
        if (Radio(ui, at(x, 2, 20, 20), s.option_list == i)) {
            s.option_list = i;
            s.disabled_choice = s.full_choice = -1;
            s.disabled_scroll = s.full_scroll = 0;
        }
        Label(ui, at(x + 20, 2, 150, 20), Get(kKinds[i]));
    }
    Label(ui, at(260, 30, 240, 20), Get(13022));
    Label(ui, at(550, 30, 240, 20), Get(13023));
    // Made again only when what they show has changed.
    static std::vector<int> disabled, full;
    static std::vector<std::string> disabled_names, full_names;
    static int cached_player = -1, cached_list = -1;
    static size_t cached_count = static_cast<size_t>(-1);
    static const Scenario::File* cached_doc = nullptr;
    const std::vector<int32_t>& now = s.option_list == 0 ? p.disabled_buildings
                                      : s.option_list == 1 ? p.disabled_units : p.disabled_techs;
    if (cached_player != s.player || cached_list != s.option_list || cached_count != now.size() ||
        cached_doc != &s.doc || !std::equal(disabled.begin(), disabled.end(), now.begin(), now.end())) {
        disabled.clear();
        full.clear();
        disabled_names.clear();
        full_names.clear();
        OptionLists(s, disabled, full, disabled_names, full_names);
        cached_player = s.player;
        cached_list = s.option_list;
        cached_count = now.size();
        cached_doc = &s.doc;
    }
    Help(ui, at(260, 50, 240, 98), 30696);
    List(ui, at(260, 50, 240, 98), disabled_names, s.disabled_choice, s.disabled_scroll);
    Help(ui, at(550, 50, 240, 98), 30695);
    List(ui, at(550, 50, 240, 98), full_names, s.full_choice, s.full_scroll);
    std::vector<int32_t>& list = s.option_list == 0 ? p.disabled_buildings
                               : s.option_list == 1 ? p.disabled_units : p.disabled_techs;
    // The table holds 30 technologies, 30 units, 20 buildings (player data
    // 1.20).
    const size_t room = s.option_list == 0 ? 20 : 30;
    Help(ui, at(510, 50, 30, 20), 30697);
    if (Button(ui, at(510, 50, 30, 20), Get(13026)) && s.full_choice >= 0 &&
        s.full_choice < static_cast<int>(full.size()) && list.size() < room) {
        list.push_back(full[static_cast<size_t>(s.full_choice)]);
        s.dirty = true;
    }
    Help(ui, at(510, 75, 30, 20), 30698);
    if (Button(ui, at(510, 75, 30, 20), Get(13027)) && s.disabled_choice >= 0 &&
        s.disabled_choice < static_cast<int>(list.size())) {
        list.erase(list.begin() + s.disabled_choice);
        s.disabled_choice = -1;
        s.dirty = true;
    }
    Help(ui, at(510, 100, 30, 20), 30699);
    if (Button(ui, at(510, 100, 30, 20), Get(13024))) {
        for (int id : full) {
            if (list.size() >= room) break;
            list.push_back(id);
        }
        s.dirty = true;
    }
    Help(ui, at(510, 125, 30, 20), 30700);
    if (Button(ui, at(510, 125, 30, 20), Get(13025))) {
        list.clear();
        s.disabled_choice = -1;
        s.dirty = true;
    }
}

// --- Messages (mode 8). ------------------------------------------------------

void MessagesPanel(State& s, Ui& ui, const At& at) {
    static constexpr int kTexts[] = {10806, 10807, 10808, 10809, 10801};
    for (int i = 0; i < 5; ++i) {
        Help(ui, at(4, 8 + 22 * i, 232, 22), 30592 + i);
        if (Radio(ui, at(4, 8 + 22 * i, 30, 22), s.message == i)) {
            s.message = i;
            LoadFields(s);
        }
        Label(ui, at(36, 8 + 22 * i, 200, 22), Get(kTexts[i]));
    }
    Scenario::Text& text = s.doc.texts[s.message];
    Label(ui, at(330, 2, 150, 20), Get(10810));
    Help(ui, at(240, 2, 90, 20), 30598);
    if (Field(ui, at(240, 2, 90, 20), s.message_string, 6, true)) {
        text.string_id = s.message_string.empty() ? -1 : Int(s.message_string);
        s.dirty = true;
    }
    // The rest of the panel: 240 in from the left, 24 from the top, 40 from
    // the right, 4 from the bottom.
    const SDL_Rect box = at(240, 24, s.frame.panel.w - 240 - 40, s.frame.panel.h - 24 - 4);
    Help(ui, box, 30592 + s.message);
    if (TextBox(ui, box, text.text, 4095)) s.dirty = true;
}

// --- Cinematics (mode 9). ----------------------------------------------------

void CinematicsPanel(State& s, Ui& ui, const At& at) {
    // What there is to play: the game's videos (GAME/AVI), none first.
    static std::vector<std::string> videos;
    if (videos.empty()) {
        videos.push_back(Get(10101));
        for (const std::string& p : Screens::FilesIn("../original_game_1999/GAME/AVI", {".avi", ".mm"})) {
            videos.push_back(std::filesystem::path(p).stem().string());
        }
    }
    std::string* names[3] = {&s.doc.pregame_cinematic, &s.doc.victory_cinematic, &s.doc.loss_cinematic};
    static constexpr int kLabels[] = {11001, 11002, 11003};
    for (int i = 0; i < 3; ++i) {
        Label(ui, at(4, 4 + 44 * i, 220, 18), Get(kLabels[i]));
        std::vector<std::string> items = videos;
        int chosen = 0;
        if (!names[i]->empty()) {
            const auto it = std::find(items.begin(), items.end(), *names[i]);
            if (it == items.end()) items.push_back(*names[i]);
            chosen = static_cast<int>(std::find(items.begin(), items.end(), *names[i]) - items.begin());
        }
        Help(ui, at(4, 22 + 44 * i, 220, 22), 30600 + i);
        if (Drop(ui, at(4, 22 + 44 * i, 220, 22), items, chosen)) {
            *names[i] = chosen == 0 ? std::string() : items[static_cast<size_t>(chosen)];
            s.dirty = true;
        }
    }
    Label(ui, at(250, 4, 220, 18), Get(11004));
    std::vector<std::string> maps = {Get(10101)};
    int map = 0;
    if (!s.doc.background.empty()) {
        maps.push_back(s.doc.background);
        map = 1;
    }
    Help(ui, at(250, 22, 220, 22), 30603);
    if (Drop(ui, at(250, 22, 220, 22), maps, map) && map == 0) {
        s.doc.background.clear();
        s.dirty = true;
    }
}

} // namespace

void LoadFields(State& s) {
    const Scenario::Player& p = s.doc.players[std::clamp(s.player, 1, Scenario::kPlayers)];
    s.food = std::to_string(p.food);
    s.wood = std::to_string(p.wood);
    s.stone = std::to_string(p.stone);
    s.gold = std::to_string(p.gold);
    s.tribe = p.name;
    s.name_string = p.name_string >= 0 ? std::to_string(p.name_string) : std::string();
    const Scenario::Text& text = s.doc.texts[std::clamp(s.message, 0, Scenario::kTextCount - 1)];
    s.message_string = text.string_id >= 0 ? std::to_string(text.string_id) : std::string();
    s.relics_text = std::to_string(s.doc.victory.relics);
    s.explored_text = std::to_string(s.doc.victory.explored);
}

void Panel(State& s, Ui& ui) {
    const At at{SDL_Point{s.frame.panel.x, s.frame.panel.y}};
    switch (s.tab) {
    case kMapTab: MapPanel(s, ui, at); break;
    case kTerrainTab: TerrainPanel(s, ui, at); break;
    case kPlayersTab: PlayersPanel(s, ui, at); break;
    case kUnitsTab: UnitsPanel(s, ui, at); break;
    case kDiplomacyTab: DiplomacyPanel(s, ui, at); break;
    case kVictoryTab: VictoryPanel(s, ui, at); break;
    case kOptionsTab: OptionsPanel(s, ui, at); break;
    case kMessagesTab: MessagesPanel(s, ui, at); break;
    case kCinematicsTab: CinematicsPanel(s, ui, at); break;
    default: break;
    }
}

} // namespace Editor
