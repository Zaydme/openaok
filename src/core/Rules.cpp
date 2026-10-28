#include "core/Rules.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace Core::Rules {

namespace {

Tables g_tables;
const std::string kEmpty;

// The file states fractions to thousandths; so does the conversion, rounding
// to the nearest so every platform lands on the same bits.
Core::Fixed32 Thousandths(double value) {
    const long long scaled = std::llround(value * 1000.0);
    return Core::Fixed32(static_cast<int>(scaled)) / Core::Fixed32(1000);
}

Core::Fixed32 Fx(const nlohmann::json& v, const char* key) {
    return v.contains(key) ? Thousandths(v[key].get<double>()) : Core::Fixed32(0);
}

int Int(const nlohmann::json& v, const char* key, int fallback) {
    return v.contains(key) ? v[key].get<int>() : fallback;
}

template <typename T>
void Grow(std::vector<T>& table, size_t id) {
    if (table.size() <= id) table.resize(id + 1);
}

int ReadClassList(const nlohmann::json& list, ClassAmount* out) {
    int n = 0;
    for (const auto& entry : list) {
        if (n >= kMaxClassEntries) break;
        out[n].cls = static_cast<int16_t>(entry[0].get<int>());
        out[n].amount = static_cast<int16_t>(entry[1].get<int>());
        ++n;
    }
    return n;
}

int ReadCosts(const nlohmann::json& list, Cost* out) {
    int n = 0;
    for (const auto& entry : list) {
        if (n >= kMaxCosts) break;
        out[n].resource = static_cast<int16_t>(entry[0].get<int>());
        out[n].amount = static_cast<int16_t>(entry[1].get<int>());
        out[n].paid = entry.size() > 2 && entry[2].get<int>() != 0;
        ++n;
    }
    return n;
}

void ReadUnit(const nlohmann::json& v, UnitRules& u, UnitText& text) {
    u.valid = true;
    text.name = v.value("name", "");
    text.label = v.value("label", "");
    u.type = Int(v, "type", 0);
    u.cls = Int(v, "class", -1);
    u.hp = Int(v, "hp", 1);
    u.los = Fx(v, "los");
    u.garrison = Int(v, "garrison", 0);
    if (v.contains("size")) {
        u.size_x = Thousandths(v["size"][0].get<double>());
        u.size_y = Thousandths(v["size"][1].get<double>());
    }
    u.speed = Fx(v, "speed");
    u.rotation = Fx(v, "rotation");
    u.enabled = Int(v, "enabled", 0) != 0;
    u.hide_in_editor = Int(v, "hide_in_editor", 0) != 0;
    u.hero = Int(v, "hero", 0) != 0;
    u.dead_unit = Int(v, "dead_unit", -1);
    u.dying_graphic = Int(v, "dying_graphic", -1);
    if (v.contains("standing")) u.standing_graphic = v["standing"][0].get<int>();
    u.walking_graphic = Int(v, "walking", -1);
    u.terrain_restriction = Int(v, "terrain_restriction", 0);
    for (int i = 0; i < 2; ++i) {
        if (v.contains("placement_side_terrain")) {
            u.placement_side_terrain[i] = v["placement_side_terrain"][i].get<int>();
        }
        if (v.contains("placement_terrain")) {
            u.placement_terrain[i] = v["placement_terrain"][i].get<int>();
        }
    }
    u.hill_mode = Int(v, "hill_mode", 0);
    u.fog_visibility = Int(v, "fog_visibility", 0);
    u.interaction = Int(v, "interaction", 0);
    u.combat_level = Int(v, "combat_level", 0);
    u.minimap_mode = Int(v, "minimap_mode", 0);
    u.carry = Int(v, "carry", 0);
    u.decay = Fx(v, "decay");

    for (const auto& s : v["storage"]) {
        if (u.storage_count >= kMaxStorage) break;
        Storage& store = u.storage[u.storage_count++];
        store.resource = static_cast<int16_t>(s[0].get<int>());
        store.amount = Thousandths(s[1].get<double>());
        store.mode = static_cast<uint8_t>(s[2].get<int>());
    }
    for (const auto& d : v["damage_graphics"]) {
        if (u.damage_graphic_count >= kMaxDamageGraphics) break;
        DamageGraphic& damage = u.damage_graphics[u.damage_graphic_count++];
        damage.graphic = static_cast<int16_t>(d[0].get<int>());
        damage.percent = static_cast<int8_t>(d[1].get<int>());
        damage.apply_mode = static_cast<int8_t>(d[2].get<int>());
    }
    if (v.contains("sounds")) {
        const auto& s = v["sounds"];
        u.sounds.select = static_cast<int16_t>(Int(s, "select", -1));
        u.sounds.dying = static_cast<int16_t>(Int(s, "dying", -1));
        u.sounds.train = static_cast<int16_t>(Int(s, "train", -1));
        u.sounds.damage = static_cast<int16_t>(Int(s, "damage", -1));
        u.sounds.attack = static_cast<int16_t>(Int(s, "attack", -1));
        u.sounds.move = static_cast<int16_t>(Int(s, "move", -1));
        u.sounds.construction = static_cast<int16_t>(Int(s, "construction", -1));
    }

    u.search_radius = Fx(v, "search_radius");
    u.work_rate = Fx(v, "work_rate");

    u.attack_count = ReadClassList(v["attacks"], u.attacks);
    u.armour_count = ReadClassList(v["armours"], u.armours);
    u.base_armour = Int(v, "base_armour", 1000);
    u.terrain_defense = Int(v, "terrain_defense", -1);
    u.max_range = Fx(v, "max_range");
    u.min_range = Fx(v, "min_range");
    u.blast_width = Fx(v, "blast_width");
    u.blast_level = Int(v, "blast_level", 0);
    u.reload = Fx(v, "reload");
    u.projectile = Int(v, "projectile", -1);
    u.accuracy = Int(v, "accuracy", 0);
    u.dispersion = Fx(v, "dispersion");
    u.frame_delay = Int(v, "frame_delay", 0);
    if (v.contains("displacement")) {
        for (int i = 0; i < 3; ++i) {
            u.displacement[i] = Thousandths(v["displacement"][i].get<double>());
        }
    }
    u.attack_graphic = Int(v, "attack_graphic", -1);
    if (v.contains("missile")) {
        const auto& m = v["missile"];
        u.missile.type = static_cast<int8_t>(Int(m, "type", 0));
        u.missile.smart = static_cast<int8_t>(Int(m, "smart", 0));
        u.missile.hit_mode = static_cast<int8_t>(Int(m, "hit", 0));
        u.missile.vanish_mode = static_cast<int8_t>(Int(m, "vanish", 0));
        u.missile.area = static_cast<int8_t>(Int(m, "area", 0));
        u.missile.arc = Fx(m, "arc");
    }

    u.cost_count = ReadCosts(v["costs"], u.costs);
    u.train_time = Int(v, "train_time", 0);
    u.train_loc = Int(v, "train_loc", -1);
    u.button = Int(v, "button", 0);
    u.missiles = Fx(v, "missiles");
    u.max_missiles = Int(v, "max_missiles", 0);
    if (v.contains("missile_area")) {
        for (int i = 0; i < 3; ++i) {
            u.missile_area[i] = Thousandths(v["missile_area"][i].get<double>());
        }
    }
    u.secondary_projectile = Int(v, "secondary_projectile", -1);

    u.construction_graphic = Int(v, "construction_graphic", -1);
    u.tech = Int(v, "tech", -1);
    u.head_unit = Int(v, "head_unit", -1);
    u.stack_unit = Int(v, "stack_unit", -1);
    u.transform_unit = Int(v, "transform_unit", -1);
    u.foundation_terrain = Int(v, "foundation_terrain", -1);
    u.garrison_type = Int(v, "garrison_type", 0);
    u.garrison_heal = Fx(v, "garrison_heal");
    u.adjacent_mode = Int(v, "adjacent_mode", 0);
    if (v.contains("clearance")) {
        u.clearance_x = Thousandths(v["clearance"][0].get<double>());
        u.clearance_y = Thousandths(v["clearance"][1].get<double>());
    }
    if (v.contains("annexes")) {
        for (const auto& a : v["annexes"]) {
            if (u.annex_count >= kMaxAnnexes) break;
            Annex& annex = u.annexes[u.annex_count++];
            annex.unit = a[0].get<int>();
            annex.dx = Thousandths(a[1].get<double>());
            annex.dy = Thousandths(a[2].get<double>());
        }
    }
    u.icon = Int(v, "icon", -1);
    if (v.contains("tasks")) {
        for (const auto& t : v["tasks"]) {
            if (u.task_count >= kMaxTasks) break;
            Task& task = u.tasks[u.task_count++];
            task.action = t[0].get<int>();
            task.target_class = t[1].get<int>();
            task.target_unit = t[2].get<int>();
            task.resource_in = t[3].get<int>();
            task.work_range = Thousandths(t[4].get<double>());
            task.work_value = Thousandths(t[5].get<double>());
            if (t.size() > 6) task.resource_out = t[6].get<int>();
            if (t.size() > 7) task.carrying_graphic = t[7].get<int>();
            if (t.size() > 8) task.auto_search = t[8].get<int>() != 0;
        }
    }
}

void ReadTech(const nlohmann::json& v, TechRules& t, TechText& text) {
    t.valid = true;
    text.name = v.value("name", "");
    text.label = v.value("label", "");
    text.help = v.value("help", "");
    for (const auto& r : v["required"]) {
        if (t.required_listed >= kMaxRequired) break;
        t.required[t.required_listed++] = r.get<int>();
    }
    t.required_count = Int(v, "required_count", 0);
    t.cost_count = ReadCosts(v["costs"], t.costs);
    t.civ = Int(v, "civ", -1);
    t.location = Int(v, "location", -1);
    t.time = Int(v, "time", 0);
    t.effect = Int(v, "effect", -1);
    t.type = Int(v, "type", 0);
    t.icon = Int(v, "icon", -1);
    t.button = Int(v, "button", 0);
}

} // namespace

bool Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[Rules] Cannot open " << path << std::endl;
        return false;
    }
    nlohmann::json data;
    in >> data;

    Tables tables;
    if (data.contains("terrain_restrictions")) {
        for (const auto& restriction : data["terrain_restrictions"]) {
            tables.terrain_restrictions.push_back(restriction.get<std::vector<int>>());
        }
    }
    for (auto it = data["units"].begin(); it != data["units"].end(); ++it) {
        const int id = std::stoi(it.key());
        if (id < 0) continue;
        Grow(tables.units, static_cast<size_t>(id));
        Grow(tables.unit_text, static_cast<size_t>(id));
        ReadUnit(it.value(), tables.units[id], tables.unit_text[id]);
    }
    for (auto it = data["techs"].begin(); it != data["techs"].end(); ++it) {
        const int id = std::stoi(it.key());
        if (id < 0) continue;
        Grow(tables.techs, static_cast<size_t>(id));
        Grow(tables.tech_text, static_cast<size_t>(id));
        ReadTech(it.value(), tables.techs[id], tables.tech_text[id]);
    }
    for (auto it = data["effects"].begin(); it != data["effects"].end(); ++it) {
        const int id = std::stoi(it.key());
        if (id < 0) continue;
        Grow(tables.effects, static_cast<size_t>(id));
        Effect& effect = tables.effects[id];
        effect.valid = true;
        for (const auto& c : it.value()["commands"]) {
            EffectCommand command;
            command.type = c[0].get<int>();
            command.a = c[1].get<int>();
            command.b = c[2].get<int>();
            command.c = c[3].get<int>();
            const double d = c[4].get<double>();
            command.d = Thousandths(d);
            command.d_int = static_cast<int>(std::llround(d));
            effect.commands.push_back(command);
        }
    }
    for (const auto& c : data["civs"]) {
        CivRules civ;
        civ.id = c["id"].get<int>();
        civ.name = c.value("name", "");
        civ.tech_tree = c.value("tech_tree", -1);
        civ.team_bonus = c.value("team_bonus", -1);
        for (const auto& r : c["resources"]) civ.resources.push_back(Thousandths(r.get<double>()));
        for (const auto& e : c["enabled"]) civ.enabled.push_back(e.get<int>());
        Grow(tables.civs, static_cast<size_t>(civ.id));
        tables.civs[civ.id] = std::move(civ);
    }

    g_tables = std::move(tables);
    std::cout << "[Rules] Loaded " << g_tables.units.size() << " unit slots, "
              << g_tables.techs.size() << " tech slots, "
              << g_tables.effects.size() << " effect slots, "
              << g_tables.civs.size() << " civs" << std::endl;
    return true;
}

const Tables& Get() { return g_tables; }

const UnitRules* Unit(int unit_id) {
    if (unit_id < 0 || unit_id >= static_cast<int>(g_tables.units.size())) return nullptr;
    const UnitRules& u = g_tables.units[unit_id];
    return u.valid ? &u : nullptr;
}

const TechRules* Tech(int tech_id) {
    if (tech_id < 0 || tech_id >= static_cast<int>(g_tables.techs.size())) return nullptr;
    const TechRules& t = g_tables.techs[tech_id];
    return t.valid ? &t : nullptr;
}

const Effect* EffectById(int effect_id) {
    if (effect_id < 0 || effect_id >= static_cast<int>(g_tables.effects.size())) {
        return nullptr;
    }
    const Effect& e = g_tables.effects[effect_id];
    return e.valid ? &e : nullptr;
}

const CivRules* Civ(int civ_id) {
    if (civ_id < 0 || civ_id >= static_cast<int>(g_tables.civs.size())) return nullptr;
    return &g_tables.civs[civ_id];
}

const std::string& UnitLabel(int unit_id) {
    if (unit_id < 0 || unit_id >= static_cast<int>(g_tables.unit_text.size())) return kEmpty;
    return g_tables.unit_text[unit_id].label;
}

const std::string& TechLabel(int tech_id) {
    if (tech_id < 0 || tech_id >= static_cast<int>(g_tables.tech_text.size())) return kEmpty;
    return g_tables.tech_text[tech_id].label;
}

int ClassValue(const ClassAmount* list, int count, int cls, int fallback) {
    for (int i = 0; i < count; ++i) {
        if (list[i].cls == cls) return list[i].amount;
    }
    return fallback;
}

bool PackingSiege(const UnitRules& rules) {
    return rules.type == UnitType::Building &&
           (rules.cls == UnitClass::PackedUnit || rules.cls == UnitClass::UnpackedSiege);
}

bool IsBuilding(const UnitRules& rules) {
    return rules.type == UnitType::Building && !PackingSiege(rules);
}

namespace {

int RestrictionValue(int restriction, int terrain) {
    const auto& table = Get().terrain_restrictions;
    if (restriction < 0 || restriction >= static_cast<int>(table.size())) return 0;
    const auto& row = table[static_cast<size_t>(restriction)];
    if (terrain < 0 || terrain >= static_cast<int>(row.size())) return 0;
    return row[static_cast<size_t>(terrain)];
}

} // namespace

bool Passable(int restriction, int terrain) {
    return RestrictionValue(restriction, terrain) > 0;
}

bool Buildable(int restriction, int terrain) {
    return RestrictionValue(restriction, terrain) > 50;
}

} // namespace Core::Rules
