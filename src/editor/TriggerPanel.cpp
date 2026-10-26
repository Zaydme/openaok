// The scenario editor's Triggers tab: the column of lists down the left and
// the fields in the panel. A trigger's fields are the ones ecs/Triggers
// carries out (scenario/Scenario.h names them).
#include <algorithm>
#include <cstdlib>

#include "core/Rules.h"
#include "core/Strings.h"
#include "ecs/components/Transform.h"
#include "editor/EditorState.h"

namespace Editor {

using Core::Strings::Get;

namespace {

int Int(const std::string& s) { return static_cast<int>(std::strtol(s.c_str(), nullptr, 10)); }

struct At {
    SDL_Point o;
    SDL_Rect operator()(int x, int y, int w, int h) const { return SDL_Rect{o.x + x, o.y + y, w, h}; }
};

// Which fields each condition and effect type uses: 20 conditions of 16
// fields, 24 effects of 23, each field 0 unused, 1 needed, 2 optional. A
// field the type uses shows its controls; the rest stay hidden.
// Conditions' fields: amount, resource, unit, next unit, object, player,
// technology, timer, trigger, area (4), group, kind, AI signal.
constexpr uint8_t kConditionUse[20][16] = {
    {},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0},   // Bring Object to Area
    {0, 0, 1, 1},                                       // Bring Object to Object
    {1, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0},   // Own Objects
    {1, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0},   // Own Fewer Objects
    {1, 0, 0, 0, 2, 1, 0, 0, 0, 1, 1, 1, 1, 2, 2, 0},   // Objects in Area
    {0, 0, 1},                                          // Destroy Object
    {0, 0, 1, 0, 0, 1},                                 // Capture Object
    {1, 1, 0, 0, 0, 1},                                 // Accumulate Attribute
    {0, 0, 0, 0, 0, 1, 1},                              // Research Technology
    {0, 0, 0, 0, 0, 0, 0, 1},                           // Timer
    {0, 0, 1},                                          // Object Selected
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},   // AI Signal
    {0, 0, 0, 0, 0, 1},                                 // Player Defeated
    {0, 0, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0},   // Object Has Target
    {0, 0, 1},                                          // Object Visible
    {0, 0, 1},                                          // Object Not Visible
    {0, 0, 0, 0, 0, 1, 1},                              // Researching Technology
    {1, 0, 1},                                          // Units Garrisoned
    {1},                                                // Difficulty Level
};
enum CondField : int {
    kCAmount, kCResource, kCUnit, kCNext, kCObject, kCPlayer, kCTech, kCTimer, kCTrigger,
    kCArea, kCGroup = 13, kCKind, kCSignal,
};
// Effects' fields: AI goal, amount, resource, diplomacy, selected objects,
// location unit, object, player, target player, technology, string id,
// sound, display time, trigger, location (2), area (4), group, kind, panel.
constexpr uint8_t kEffectUse[24][23] = {
    {},
    {0, 0, 0, 1, 0, 0, 0, 1, 1},                                            // Change Diplomacy
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1},                                         // Research Technology
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 2},                                   // Send Chat
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1},                                   // Play Sound
    {0, 1, 1, 0, 0, 0, 0, 1, 1},                                            // Tribute
    {0, 0, 0, 0, 1},                                                        // Unlock Gate
    {0, 0, 0, 0, 1},                                                        // Lock Gate
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},                             // Activate Trigger
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},                             // Deactivate Trigger
    {1, 0, 0, 0, 0, 0, 0, 1},                                               // AI Script Goal
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1},                       // Create Object
    {0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2},     // Task Object
    {0, 0, 0, 0, 0, 0, 0, 1},                                               // Declare Victory
    {0, 0, 0, 0, 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2},     // Kill Object
    {0, 0, 0, 0, 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2},     // Remove Object
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1},                       // Change View
    {0, 0, 0, 0, 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2},     // Unload
    {0, 0, 0, 0, 2, 0, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2},     // Change Ownership
    {0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2},                       // Patrol
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},  // Display Instructions
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},  // Clear Instructions
    {0, 0, 0, 0, 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2},     // Freeze Unit
    {},                                                                     // Use Advanced Buttons
};
enum EffField : int {
    kEGoal, kEAmount, kEResource, kEDiplomacy, kESelected, kELocationUnit, kEObject, kEPlayer,
    kETarget, kETech, kEString, kESound, kETime, kETrigger, kELocation, kEArea = 16, kEGroup = 20,
    kEKind, kEPanel,
};

// The lists' contents: players from Gaia, the attributes with
// their dat resources, the object list types, diplomacy, tribute.
std::vector<std::string> Players() {
    std::vector<std::string> out = {Get(10102)};
    for (int p = 1; p <= Scenario::kPlayers; ++p) out.push_back(Get(10340 + p));
    return out;
}
struct Valued {
    int string;
    int value;
};
constexpr Valued kAttributes[] = {{10332, 0},  {10333, 1},  {10331, 3},  {10334, 2},  {10335, 11},
                                  {10370, 43}, {10371, 41}, {10372, 44}, {10374, 40}, {10375, 21},
                                  {10376, 20}, {10377, 37}};
constexpr Valued kTributes[] = {{10332, 0}, {10333, 1}, {10331, 3}, {10334, 2}};
constexpr Valued kStances[] = {{10701, 0}, {10702, 1}, {10703, 3}};
constexpr Valued kKinds[] = {{10101, -1}, {10760, 2}, {10761, 3}, {10762, 4}, {10763, 1}};
constexpr Valued kListTypes[] = {{10765, kUnitsList}, {10766, kBuildingsList}, {10767, kHeroesList},
                                 {10768, kOtherList}};

template <size_t N>
bool ValuedDrop(Ui& ui, const SDL_Rect& r, const Valued (&table)[N], int32_t& value) {
    std::vector<std::string> items;
    int at = 0;
    for (size_t i = 0; i < N; ++i) {
        items.push_back(Core::Strings::Get(table[i].string));
        if (table[i].value == value) at = static_cast<int>(i);
    }
    if (!Drop(ui, r, items, at)) return false;
    value = table[at].value;
    return true;
}

bool PlayerDrop(Ui& ui, const SDL_Rect& r, int32_t& player) {
    static const auto players = Players();
    int at = std::clamp(player, 0, Scenario::kPlayers);
    if (!Drop(ui, r, players, at)) return false;
    player = at;
    return true;
}

Scenario::Trigger* Selected(State& s) {
    if (s.trigger < 0 || s.trigger >= static_cast<int>(s.doc.triggers.size())) return nullptr;
    return &s.doc.triggers[static_cast<size_t>(s.trigger)];
}

// The conditions and effects list: conditions first, each in display order.
std::vector<std::string> Lines(const Scenario::Trigger& t) {
    std::vector<std::string> out;
    for (const auto& c : t.conditions) out.push_back("C: " + Get(10500 + std::clamp(c.type, 0, 19)));
    for (const auto& e : t.effects) out.push_back("E: " + Get(10550 + std::clamp(e.type, 0, 23)));
    return out;
}

void LoadTriggerFields(State& s) {
    const Scenario::Trigger* t = Selected(s);
    if (t == nullptr) return;
    s.trigger_name = t->name;
    s.trigger_description = t->description;
    s.trigger_string = t->string_id >= 0 ? std::to_string(t->string_id) : std::string();
    s.trigger_order = std::to_string(t->objective_order);
    const int conditions = static_cast<int>(t->conditions.size());
    if (s.item >= conditions && s.item < conditions + static_cast<int>(t->effects.size())) {
        const Scenario::Effect& e = t->effects[static_cast<size_t>(s.item - conditions)];
        s.line_message = e.message;
        s.line_string = e.string_id >= 0 ? std::to_string(e.string_id) : std::string();
        s.line_sound = e.sound;
        s.line_amount = std::to_string(std::max(0, e.amount));
        s.line_timer = std::to_string(std::max(0, e.display_time));
        s.line_sound_id = e.sound_id >= 0 ? std::to_string(e.sound_id) : std::string();
        s.line_panel = std::to_string(std::max(0, e.panel));
    } else if (s.item >= 0 && s.item < conditions) {
        const Scenario::Condition& c = t->conditions[static_cast<size_t>(s.item)];
        s.line_amount = std::to_string(std::max(0, c.amount));
        s.line_timer = std::to_string(std::max(0, c.timer));
    }
}

// A list's lines and what each stands for, made once.
struct Choices {
    std::vector<int> ids;
    std::vector<std::string> names;
    int IndexOf(int id) const {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) return static_cast<int>(i);
        }
        return 0;
    }
};

// The technologies a trigger names, none first.
const Choices& TechChoices() {
    static Choices c;
    if (!c.ids.empty()) return c;
    c.ids.push_back(-1);
    c.names.push_back(Get(10101));
    const auto& techs = Core::Rules::Get().techs;
    for (int id = 0; id < static_cast<int>(techs.size()); ++id) {
        if (!techs[static_cast<size_t>(id)].valid || Core::Rules::TechLabel(id).empty()) continue;
        c.ids.push_back(id);
        c.names.push_back(Core::Rules::TechLabel(id));
    }
    return c;
}

// "AI Signal %d" 0-254, "AI Trigger %d" 0-255.
const std::vector<std::string>& Numbered(bool signal) {
    static std::vector<std::string> lists[2];
    std::vector<std::string>& list = lists[signal ? 1 : 0];
    if (list.empty()) {
        for (int i = 0; i < (signal ? 255 : 256); ++i) list.push_back(Get(signal ? 10773 : 10708) + " " + std::to_string(i));
    }
    return list;
}

// The dat's unit classes by name (10400 on), none first.
const std::vector<std::string>& Classes() {
    static std::vector<std::string> list;
    if (list.empty()) {
        list.push_back(Get(10101));
        for (int k = 0; k < 64; ++k) list.push_back(Get(10400 + k));
    }
    return list;
}

// The units a list of objects offers for its type.
void ObjectChoices(int type, std::vector<int>& ids, std::vector<std::string>& names) {
    const auto& units = Core::Rules::Get().units;
    names.push_back(Get(10101));
    ids.push_back(-1);
    for (int id = 0; id < static_cast<int>(units.size()); ++id) {
        const auto& u = units[static_cast<size_t>(id)];
        if (!u.valid || u.hide_in_editor || Core::Rules::UnitLabel(id).empty()) continue;
        const bool building = u.type == 80, creatable = u.type == 70;
        const bool fits = type == kBuildingsList ? building
                        : type == kHeroesList ? creatable && u.hero
                        : type == kOtherList ? !building && !creatable
                        : creatable && !u.hero;
        if (!fits) continue;
        ids.push_back(id);
        names.push_back(Core::Rules::UnitLabel(id));
    }
}

} // namespace

void TriggerColumn(State& s, Ui& ui) {
    const Frame& f = s.frame;
    std::vector<std::string> names;
    for (const auto& t : s.doc.triggers) names.push_back(t.name);
    Label(ui, f.trig_label, Get(10715));
    Help(ui, f.trig_list, 30710);
    if (List(ui, f.trig_list, names, s.trigger, s.trigger_scroll)) {
        s.item = -1;
        s.trigger_info = true;
        LoadTriggerFields(s);
    }
    Scenario::Trigger* t = Selected(s);
    auto& triggers = s.doc.triggers;
    auto& order = s.doc.trigger_order;
    // Up and down move the selected trigger in the list.
    Help(ui, f.trig_up, 30714);
    if (Button(ui, f.trig_up, "^", t != nullptr && s.trigger > 0)) {
        std::swap(triggers[static_cast<size_t>(s.trigger)], triggers[static_cast<size_t>(s.trigger - 1)]);
        --s.trigger;
        s.dirty = true;
    }
    Help(ui, f.trig_down, 30715);
    if (Button(ui, f.trig_down, "v", t != nullptr && s.trigger + 1 < static_cast<int>(triggers.size()))) {
        std::swap(triggers[static_cast<size_t>(s.trigger)], triggers[static_cast<size_t>(s.trigger + 1)]);
        ++s.trigger;
        s.dirty = true;
    }
    Help(ui, f.trig_buttons[0], 30711);
    if (Button(ui, f.trig_buttons[0], Get(10709), t != nullptr, s.trigger_info && t != nullptr)) {
        s.trigger_info = true;
        s.item = -1;
    }
    Help(ui, f.trig_buttons[1], 30712);
    if (Button(ui, f.trig_buttons[1], Get(10710))) {
        Scenario::Trigger fresh;
        fresh.name = "Trigger " + std::to_string(triggers.size());
        triggers.push_back(fresh);
        s.trigger = static_cast<int>(triggers.size()) - 1;
        s.item = -1;
        s.trigger_info = true;
        s.dirty = true;
        LoadTriggerFields(s);
    }
    Help(ui, f.trig_buttons[2], 30713);
    if (Button(ui, f.trig_buttons[2], Get(10711), t != nullptr)) {
        // Triggers named by number elsewhere follow the ones that move up.
        const int gone = s.trigger;
        triggers.erase(triggers.begin() + gone);
        for (auto& other : triggers) {
            for (auto& e : other.effects) {
                if (e.trigger == gone) e.trigger = -1;
                else if (e.trigger > gone) --e.trigger;
            }
        }
        s.trigger = std::min(s.trigger, static_cast<int>(triggers.size()) - 1);
        s.item = -1;
        s.dirty = true;
        LoadTriggerFields(s);
    }
    // The display order is the list's order.
    order.clear();
    for (size_t i = 0; i < triggers.size(); ++i) order.push_back(static_cast<int32_t>(i));

    t = Selected(s);
    Label(ui, f.ce_label, Get(10716));
    std::vector<std::string> lines = t != nullptr ? Lines(*t) : std::vector<std::string>{};
    Help(ui, f.ce_list, 30716);
    if (List(ui, f.ce_list, lines, s.item, s.item_scroll)) {
        s.trigger_info = false;
        LoadTriggerFields(s);
    }
    Help(ui, f.ce_buttons[0], 30719);
    if (Button(ui, f.ce_buttons[0], Get(10712), t != nullptr)) {
        t->conditions.push_back(Scenario::Condition{});
        t->condition_order.push_back(static_cast<int32_t>(t->conditions.size()) - 1);
        s.item = static_cast<int>(t->conditions.size()) - 1;
        s.trigger_info = false;
        s.dirty = true;
        LoadTriggerFields(s);
    }
    Help(ui, f.ce_buttons[1], 30720);
    if (Button(ui, f.ce_buttons[1], Get(10713), t != nullptr)) {
        t->effects.push_back(Scenario::Effect{});
        t->effect_order.push_back(static_cast<int32_t>(t->effects.size()) - 1);
        s.item = static_cast<int>(t->conditions.size() + t->effects.size()) - 1;
        s.trigger_info = false;
        s.dirty = true;
        LoadTriggerFields(s);
    }
    Help(ui, f.ce_buttons[2], 30721);
    if (Button(ui, f.ce_buttons[2], Get(10714), t != nullptr && s.item >= 0)) {
        const int conditions = static_cast<int>(t->conditions.size());
        if (s.item < conditions) {
            t->conditions.erase(t->conditions.begin() + s.item);
        } else if (s.item - conditions < static_cast<int>(t->effects.size())) {
            t->effects.erase(t->effects.begin() + (s.item - conditions));
        }
        t->condition_order.clear();
        for (size_t i = 0; i < t->conditions.size(); ++i) t->condition_order.push_back(static_cast<int32_t>(i));
        t->effect_order.clear();
        for (size_t i = 0; i < t->effects.size(); ++i) t->effect_order.push_back(static_cast<int32_t>(i));
        s.item = -1;
        s.trigger_info = true;
        s.dirty = true;
    }
}

void TriggerPanel(State& s, Ui& ui) {
    const At at{SDL_Point{s.frame.panel.x, s.frame.panel.y}};
    Scenario::Trigger* t = Selected(s);
    if (t == nullptr) return;
    if (s.trigger_info || s.item < 0) {
        // The trigger's own fields.
        Label(ui, at(2, 4, 175, 22), Get(10718));
        Help(ui, at(2, 26, 175, 22), 30725);
        if (Field(ui, at(2, 26, 175, 22), s.trigger_name, 128)) {
            t->name = s.trigger_name;
            s.dirty = true;
        }
        Label(ui, at(2, 48, 175, 22), Get(10720));
        Help(ui, at(2, 70, 175, 22), 30724);
        if (Field(ui, at(2, 70, 175, 22), s.trigger_string, 10, true)) {
            t->string_id = s.trigger_string.empty() ? -1 : Int(s.trigger_string);
            s.dirty = true;
        }
        Label(ui, at(2, 92, 175, 22), Get(10770));
        Help(ui, at(2, 114, 175, 22), 30730);
        if (Field(ui, at(2, 114, 175, 22), s.trigger_order, 6, true)) {
            t->objective_order = Int(s.trigger_order);
            s.dirty = true;
        }
        Label(ui, at(180, 2, 200, 22), Get(10719));
        Help(ui, at(180, 24, 200, 100), 30723);
        if (TextBox(ui, at(180, 24, 200, 100), s.trigger_description, 4095)) {
            t->description = s.trigger_description;
            s.dirty = true;
        }
        // Each pair's help, yes then no.
        struct YesNo { int label, y, yes, no; bool* value; int help; };
        YesNo rows[] = {{10769, 4, 10754, 10755, &t->objective, 30731},
                        {10750, 48, 10751, 10752, &t->enabled, 30726},
                        {10753, 92, 10754, 10755, &t->looping, 30728}};
        for (YesNo& row : rows) {
            Label(ui, at(390, row.y, 180, 22), Get(row.label));
            Help(ui, at(390, row.y + 22, 60, 22), row.help);
            if (Button(ui, at(390, row.y + 22, 60, 22), Get(row.yes), true, *row.value)) {
                *row.value = true;
                s.dirty = true;
            }
            Help(ui, at(450, row.y + 22, 60, 22), row.help + 1);
            if (Button(ui, at(450, row.y + 22, 60, 22), Get(row.no), true, !*row.value)) {
                *row.value = false;
                s.dirty = true;
            }
        }
        return;
    }

    const int conditions = static_cast<int>(t->conditions.size());
    const bool is_condition = s.item < conditions;
    if (!is_condition && s.item - conditions >= static_cast<int>(t->effects.size())) return;
    Scenario::Condition* c = is_condition ? &t->conditions[static_cast<size_t>(s.item)] : nullptr;
    Scenario::Effect* e = is_condition ? nullptr : &t->effects[static_cast<size_t>(s.item - conditions)];

    // The condition's or effect's type.
    Label(ui, at(4, 2, 200, 20), Get(is_condition ? 10721 : 10722));
    std::vector<std::string> types;
    const int type_count = is_condition ? 20 : 24;
    for (int i = 0; i < type_count; ++i) types.push_back(Get((is_condition ? 10500 : 10550) + i));
    int type = is_condition ? c->type : e->type;
    Help(ui, at(4, 22, 200, 20), is_condition ? 30733 : 30734);
    if (Drop(ui, at(4, 22, 200, 20), types, type)) {
        if (is_condition) *c = Scenario::Condition{.type = type};
        else *e = Scenario::Effect{.type = type};
        s.dirty = true;
        LoadTriggerFields(s);
        return;
    }
    auto uses = [&](int field) {
        return is_condition ? kConditionUse[std::clamp(type, 0, 19)][field] != 0
                            : kEffectUse[std::clamp(type, 0, 23)][field] != 0;
    };
    auto changed = [&] { s.dirty = true; };
    int32_t& player = is_condition ? c->player : e->player;
    int32_t& object = is_condition ? c->object : e->object;
    int32_t& group = is_condition ? c->group : e->group;
    int32_t& kind = is_condition ? c->kind : e->kind;
    Scenario::Area& area = is_condition ? c->area : e->area;
    const bool gate = !is_condition && (type == Scenario::EffectType::UnlockGate ||
                                        type == Scenario::EffectType::LockGate);

    // What to point at in the map view, as a pair of buttons: Set and Go to.
    auto pick_pair = [&](int y, int set, int go, Pick mode, bool can_go) {
        const int pick_help = mode == Pick::Area ? 30736 : mode == Pick::Location ? 30738
                            : mode == Pick::Object ? 30740 : mode == Pick::NextObject ? 30742 : 30744;
        Help(ui, at(385, y, 175, 20), pick_help);
        if (Button(ui, at(385, y, 175, 20), Get(set), true, s.pick == mode)) {
            s.pick = mode;
            s.pick_corner = 0;
        }
        Help(ui, at(385, y + 20, 175, 20), pick_help + 1);
        if (Button(ui, at(385, y + 20, 175, 20), Get(go), can_go)) {
            // The file's tiles and places, turned onto the world's.
            if (mode == Pick::Area && area.Set()) {
                const auto [ax, ay] = GridTile(s, area.x0, area.y0);
                const auto [bx, by] = GridTile(s, area.x1, area.y1);
                CentreOn(s, (ax + bx) / 2.0f, (ay + by) / 2.0f);
            } else if (mode == Pick::Location && e != nullptr && e->location.Set()) {
                const auto [gx, gy] = GridTile(s, e->location.x, e->location.y);
                CentreOn(s, static_cast<float>(gx), static_cast<float>(gy));
            } else {
                const int32_t id = mode == Pick::Object ? (is_condition ? c->unit : e->location_unit)
                                 : mode == Pick::NextObject ? c->next_unit
                                 : (e != nullptr && !e->units.empty() ? e->units.front() : -1);
                if (const Scenario::Unit* u = s.doc.FindUnit(id)) {
                    const auto world = WorldOf(s, u->x, u->y);
                    CentreOn(s, world.x.ToFloat(), world.y.ToFloat());
                }
            }
        }
    };

    // Each field's controls.
    if (is_condition ? uses(kCPlayer) : uses(kEPlayer)) {
        Label(ui, at(230, 2, 150, 20), Get(10743));
        Help(ui, at(230, 22, 150, 20), 30722);
        if (PlayerDrop(ui, at(230, 22, 150, 20), player)) changed();
    }
    if (is_condition ? uses(kCTimer) : uses(kETime)) {
        // The timer, which is an instruction's display time too.
        Label(ui, at(230, 2, 150, 20), Get(10723));
        Help(ui, at(230, 22, 90, 20), 30735);
        if (Field(ui, at(230, 22, 90, 20), s.line_timer, 6, true)) {
            (is_condition ? c->timer : e->display_time) = Int(s.line_timer);
            changed();
        }
    }
    if (is_condition && type == Scenario::ConditionType::DifficultyLevel) {
        Label(ui, at(230, 2, 150, 20), Get(10774));
        static const std::vector<std::string> levels = {Get(11216), Get(11217), Get(11218), Get(11219), Get(11220)};
        int i = std::clamp(c->amount, 0, 4);
        Help(ui, at(230, 22, 150, 20), 30764);
        if (Drop(ui, at(230, 22, 150, 20), levels, i)) { c->amount = i; changed(); }
    } else if (is_condition ? uses(kCAmount) : uses(kEAmount)) {
        Label(ui, at(385, 96, 150, 20), Get(10731));
        Help(ui, at(385, 116, 90, 20), 30748);
        if (Field(ui, at(385, 116, 90, 20), s.line_amount, 6, true)) {
            (is_condition ? c->amount : e->amount) = Int(s.line_amount);
            changed();
        }
    }
    if (is_condition ? uses(kCResource) : uses(kEResource)) {
        if (!is_condition && type == Scenario::EffectType::SendTribute) {
            Label(ui, at(4, 60, 200, 20), Get(10744));
            Help(ui, at(4, 80, 200, 20), 30752);
            if (ValuedDrop(ui, at(4, 80, 200, 20), kTributes, e->resource)) changed();
        } else {
            Label(ui, at(4, 60, 200, 20), Get(10734));
            Help(ui, at(4, 80, 200, 20), 30751);
            if (ValuedDrop(ui, at(4, 80, 200, 20), kAttributes, is_condition ? c->resource : e->resource)) changed();
        }
    }
    if (is_condition ? uses(kCTech) : uses(kETech)) {
        Label(ui, at(4, 60, 200, 20), Get(10732));
        const Choices& techs = TechChoices();
        int32_t& tech = is_condition ? c->technology : e->technology;
        int chosen = techs.IndexOf(tech);
        Help(ui, at(4, 80, 200, 20), 30749);
        if (Drop(ui, at(4, 80, 200, 20), techs.names, chosen)) { tech = techs.ids[static_cast<size_t>(chosen)]; changed(); }
    }
    if ((is_condition && uses(kCSignal)) || (!is_condition && uses(kEGoal))) {
        const bool signal = is_condition;
        Label(ui, at(4, 60, 200, 20), Get(signal ? 10772 : 10739));
        int32_t& v = signal ? c->ai_signal : e->ai_goal;
        int i = std::max(0, v);
        Help(ui, at(4, 80, 200, 20), signal ? 30763 : 30759);
        if (Drop(ui, at(4, 80, 200, 20), Numbered(signal), i)) { v = i; changed(); }
    }
    if (is_condition ? uses(kCTrigger) : uses(kETrigger)) {
        Label(ui, at(4, 60, 200, 20), Get(10742));
        std::vector<std::string> names = {Get(10101)};
        for (const auto& other : s.doc.triggers) names.push_back(other.name);
        int32_t& v = is_condition ? c->trigger : e->trigger;
        int i = v + 1;
        Help(ui, at(4, 80, 200, 20), 30753);
        if (Drop(ui, at(4, 80, 200, 20), names, i)) { v = i - 1; changed(); }
    }
    if (is_condition ? uses(kCGroup) : uses(kEGroup)) {
        Label(ui, at(4, 60, 200, 20), Get(10756));
        int at_class = group + 1;
        Help(ui, at(4, 80, 200, 20), 30760);
        if (Drop(ui, at(4, 80, 200, 20), Classes(), at_class)) { group = at_class - 1; changed(); }
    }
    if (is_condition ? uses(kCKind) : uses(kEKind)) {
        Label(ui, at(230, 103, 150, 20), Get(10757));
        Help(ui, at(230, 123, 150, 20), 30761);
        if (ValuedDrop(ui, at(230, 123, 150, 20), kKinds, kind)) changed();
    }
    if (!is_condition && uses(kETarget)) {
        Label(ui, at(230, 45, 150, 20), Get(10733));
        Help(ui, at(230, 65, 150, 20), 30750);
        if (PlayerDrop(ui, at(230, 65, 150, 20), e->target_player)) changed();
    }
    if (!is_condition && uses(kEDiplomacy)) {
        Label(ui, at(385, 2, 150, 20), Get(10738));
        Help(ui, at(385, 22, 150, 20), 30758);
        if (ValuedDrop(ui, at(385, 22, 150, 20), kStances, e->diplomacy)) changed();
    }
    if (!is_condition && uses(kEString)) {
        // A message and its string.
        Label(ui, at(230, 45, 150, 20), Get(10737));
        Help(ui, at(230, 65, 330, 60), 30756);
        if (TextBox(ui, at(230, 65, 330, 60), s.line_message, 4095)) { e->message = s.line_message; changed(); }
        Label(ui, at(385, 2, 120, 20), Get(10720));
        Help(ui, at(385, 22, 120, 20), 30757);
        if (Field(ui, at(385, 22, 120, 20), s.line_string, 10, true)) {
            e->string_id = s.line_string.empty() ? -1 : Int(s.line_string);
            changed();
        }
    }
    if (!is_condition && uses(kESound)) {
        // The sound file and its resource id.
        Label(ui, at(4, 60, 200, 20), Get(10736));
        Help(ui, at(4, 80, 200, 20), 30754);
        if (Field(ui, at(4, 80, 200, 20), s.line_sound, 64)) { e->sound = s.line_sound; changed(); }
        Label(ui, at(4, 103, 150, 20), Get(10747));
        Help(ui, at(4, 123, 90, 20), 30755);
        if (Field(ui, at(4, 123, 90, 20), s.line_sound_id, 6, true)) {
            e->sound_id = s.line_sound_id.empty() ? -1 : Int(s.line_sound_id);
            changed();
        }
    }
    if (!is_condition && uses(kEPanel)) {
        Label(ui, at(510, 2, 90, 20), Get(10771));
        Help(ui, at(510, 22, 40, 20), 30762);
        if (Field(ui, at(510, 22, 40, 20), s.line_panel, 1, true)) { e->panel = Int(s.line_panel); changed(); }
    }
    if (is_condition ? uses(kCObject) : uses(kEObject)) {
        // The Object List, of the type chosen.
        static int list_type = kUnitsList;
        Label(ui, at(230, 60, 150, 20), Get(10764));
        int32_t lt = list_type;
        Help(ui, at(230, 80, 150, 20), 30746);
        if (ValuedDrop(ui, at(230, 80, 150, 20), kListTypes, lt)) list_type = lt;
        static Choices by_type[5];
        Choices& choices = by_type[std::clamp(list_type, 0, 4)];
        if (choices.ids.empty()) ObjectChoices(list_type, choices.ids, choices.names);
        int chosen = choices.IndexOf(object);
        Label(ui, at(4, 103, 200, 20), Get(10730));
        Help(ui, at(4, 123, 200, 20), 30747);
        if (Drop(ui, at(4, 123, 200, 20), choices.names, chosen)) {
            object = choices.ids[static_cast<size_t>(chosen)];
            changed();
        }
    }
    if (is_condition && uses(kCUnit)) pick_pair(2, 10726, 10727, Pick::Object, c->unit >= 0);
    if (!is_condition && (uses(kESelected) || uses(kELocationUnit))) {
        // A gate's own pair, else Set Objects.
        if (gate) pick_pair(2, 10745, 10746, Pick::Objects, !e->units.empty());
        else pick_pair(2, 10758, 10759, Pick::Objects, !e->units.empty());
    }
    if (is_condition && uses(kCNext)) pick_pair(49, 10728, 10729, Pick::NextObject, c->next_unit >= 0);
    if (is_condition ? uses(kCArea) : uses(kEArea)) pick_pair(49, 10724, 10725, Pick::Area, area.Set());
    if (!is_condition && uses(kELocation)) pick_pair(96, 10748, 10749, Pick::Location, e->location.Set());
}

bool PickInView(State& s, float wx, float wy) {
    if (s.pick == Pick::None) return false;
    Scenario::Trigger* t = Selected(s);
    if (t == nullptr) {
        s.pick = Pick::None;
        return false;
    }
    const int conditions = static_cast<int>(t->conditions.size());
    Scenario::Condition* c = s.item >= 0 && s.item < conditions ? &t->conditions[static_cast<size_t>(s.item)] : nullptr;
    Scenario::Effect* e = s.item >= conditions && s.item - conditions < static_cast<int>(t->effects.size())
                              ? &t->effects[static_cast<size_t>(s.item - conditions)] : nullptr;
    // The file's tile under the point of the world.
    const auto [tx, ty] = FileTile(s, static_cast<int>(wx + 0.5f), static_cast<int>(wy + 0.5f));
    switch (s.pick) {
    case Pick::Area: {
        Scenario::Area& area = c != nullptr ? c->area : e->area;
        if (s.pick_corner == 0) {
            area = Scenario::Area{tx, ty, tx, ty};
            s.pick_corner = 1;
            return true;
        }
        area.x1 = tx;
        area.y1 = ty;
        // The lower corner first.
        if (area.x0 > area.x1) std::swap(area.x0, area.x1);
        if (area.y0 > area.y1) std::swap(area.y0, area.y1);
        break;
    }
    case Pick::Location:
        if (e != nullptr) {
            e->location = Scenario::Point{tx, ty};
            // A unit of the file clicked is the target, for an effect that
            // takes one (Task Object's location unit).
            if (kEffectUse[std::clamp(e->type, 0, 23)][kELocationUnit] != 0) {
                const auto it = s.refs.find(static_cast<uint32_t>(ThingAt(s, wx, wy)));
                e->location_unit = it != s.refs.end() ? it->second.id : -1;
            }
        }
        break;
    case Pick::Object:
    case Pick::NextObject:
    case Pick::Objects: {
        const entt::entity thing = ThingAt(s, wx, wy);
        const auto it = s.refs.find(static_cast<uint32_t>(thing));
        if (it == s.refs.end()) return true;
        const int32_t id = it->second.id;
        if (s.pick == Pick::Object) {
            if (c != nullptr) c->unit = id;
            else if (e != nullptr) e->location_unit = id;
        } else if (s.pick == Pick::NextObject) {
            if (c != nullptr) c->next_unit = id;
        } else if (e != nullptr) {
            // Each click adds one more.
            if (std::find(e->units.begin(), e->units.end(), id) == e->units.end()) e->units.push_back(id);
            e->selected_count = static_cast<int32_t>(e->units.size());
            s.dirty = true;
            return true;
        }
        break;
    }
    default:
        break;
    }
    s.pick = Pick::None;
    s.dirty = true;
    return true;
}

} // namespace Editor
