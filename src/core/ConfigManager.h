#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "core/FixedPoint.h"
#include "ecs/components/Sprite.h"

namespace Core {

class ConfigManager {
public:
    static ConfigManager& Get() {
        static ConfigManager instance;
        return instance;
    }

    bool LoadExtractedData(const std::string& units_path, const std::string& graphics_path) {
        std::ifstream uf(units_path);
        std::ifstream gf(graphics_path);
        if (!uf.is_open() || !gf.is_open()) {
            std::cerr << "Failed to open extracted json files." << std::endl;
            return false;
        }
        try {
            uf >> units_json;
            gf >> graphics_json;
        } catch (...) { return false; }

        // Index by name once. The dat stores some names with an "_NN" suffix
        // (the generic civ/architecture variant), so both spellings resolve.
        for (auto it = units_json.begin(); it != units_json.end(); ++it) {
            const std::string name = it.value().value("name", std::string{});
            if (name.empty()) continue;
            unit_by_name.emplace(name, it.key());
            if (name.size() > 3 && name.compare(name.size() - 3, 3, "_NN") == 0) {
                unit_by_name.emplace(name.substr(0, name.size() - 3), it.key());
            }
        }
        for (auto it = graphics_json.begin(); it != graphics_json.end(); ++it) {
            const std::string name = it.value().value("name", std::string{});
            if (!name.empty()) graphic_by_name.emplace(name, it.key());
        }

        std::cout << "[ConfigManager] Loaded authentic dat files: "
                  << units_json.size() << " units, "
                  << graphics_json.size() << " graphics." << std::endl;
        return true;
    }

    // A graphic as the dat describes it. frame_count is frames per angle;
    // angle_count is the logical facing count (8 for units), of which the SLP
    // physically stores 5 and mirrors the other 3.
    struct GraphicInfo {
        int graphic_id = -1;
        int slp_id = -1;
        int frame_count = 0;
        int angle_count = 0;
        int layer = 0;   // the dat's draw layer: a ship's wake 10 under its hull 20
        bool Valid() const { return slp_id >= 0; }
    };

    GraphicInfo GetGraphic(int graphic_id) const {
        GraphicInfo info;
        if (graphic_id < 0) return info;
        auto it = graphics_json.find(std::to_string(graphic_id));
        if (it == graphics_json.end()) return info;
        info.graphic_id = graphic_id;
        info.slp_id = it->value("slp_id", -1);
        info.frame_count = it->value("frame_count", 0);
        info.angle_count = it->value("angle_count", 0);
        info.layer = it->value("layer", 0);
        return info;
    }

    // Look a graphic up by its dat name, e.g. "RTWC1NNG" for the Dark Age
    // Town Center body. Building art is named <unit><age><variant><civ>.
    GraphicInfo GetGraphicByName(const std::string& name) const {
        auto it = graphic_by_name.find(name);
        if (it == graphic_by_name.end()) return GraphicInfo{};
        auto g = graphics_json.find(it->second);
        if (g == graphics_json.end()) return GraphicInfo{};
        GraphicInfo info;
        info.graphic_id = std::stoi(it->second);
        info.slp_id = g->value("slp_id", -1);
        info.frame_count = g->value("frame_count", 0);
        info.angle_count = g->value("angle_count", 0);
        return info;
    }

    // The right-hand counterpart of a unit's standing graphic: a building piece
    // named <base>N<k><civ> for k in 2..4 has its other half at
    // <base>N<k+3><civ>, which no unit owns. Invalid when there is none.
    GraphicInfo GetCounterpartGraphic(int unit_id) const {
        const GraphicInfo own = GetUnitGraphicById(unit_id, "idle_graphic0");
        if (!own.Valid()) return GraphicInfo{};
        auto g = graphics_json.find(std::to_string(own.graphic_id));
        if (g == graphics_json.end()) return GraphicInfo{};
        std::string name = g->value("name", "");
        for (size_t pos = 1; pos <= 2 && pos < name.size(); ++pos) {
            const size_t i = name.size() - pos;
            if (i >= 1 && name[i - 1] == 'N' && name[i] >= '2' && name[i] <= '4') {
                name[i] = static_cast<char>(name[i] + 3);
                return GetGraphicByName(name);
            }
        }
        return GraphicInfo{};
    }

    // Resolve one of a unit's animation graphics, e.g.
    // GetUnitGraphic("VMBAS", "walking_graphic0").
    GraphicInfo GetUnitGraphic(const std::string& dat_name,
                               const std::string& field) const {
        auto it = unit_by_name.find(dat_name);
        if (it == unit_by_name.end()) return GraphicInfo{};
        auto u = units_json.find(it->second);
        if (u == units_json.end()) return GraphicInfo{};
        // The attack graphic is not in units.json; it comes from animations.json.
        if (field == "attack_graphic") {
            auto a = attack_graphic.find(u->value("id", -1));
            return a == attack_graphic.end() ? GraphicInfo{} : GetGraphic(a->second);
        }
        // Nor is the work graphic (hammering, chopping, mining), which comes
        // from the dat's task list.
        if (field == "work_graphic") {
            auto w = work_graphic.find(u->value("id", -1));
            return w == work_graphic.end() ? GraphicInfo{} : GetGraphic(w->second);
        }
        if (field == "carry_graphic") {
            auto c = carry_graphic.find(u->value("id", -1));
            return c == carry_graphic.end() ? GraphicInfo{} : GetGraphic(c->second);
        }
        return GetGraphic(u->value(field, -1));
    }

    // Per-unit attack graphics and per-graphic frame timing, neither of which
    // units.json carries. See tools/data/extract_animation_data.py.
    bool LoadAnimations(const std::string& filepath) {
        std::ifstream af(filepath);
        if (!af.is_open()) {
            std::cerr << "[ConfigManager] Failed to open animations: " << filepath << std::endl;
            return false;
        }
        nlohmann::json j;
        try { af >> j; } catch (...) { return false; }
        for (auto it = j["units"].begin(); it != j["units"].end(); ++it) {
            const int id = std::stoi(it.key());
            attack_graphic[id] = it.value().value("attack", -1);
            if (it.value().contains("work")) {
                work_graphic[id] = it.value().value("work", -1);
            }
            if (it.value().contains("carry")) {
                carry_graphic[id] = it.value().value("carry", -1);
            }
            if (it.value().contains("work_range")) {
                work_range[id] = ToFixed(it.value().value("work_range", 0.0));
            }
        }
        for (auto it = j["graphics"].begin(); it != j["graphics"].end(); ++it) {
            frame_ms[std::stoi(it.key())] = it.value().value("frame_ms", 0);
        }
        std::cout << "[ConfigManager] Loaded " << attack_graphic.size()
                  << " attack graphics, " << frame_ms.size()
                  << " animation timings." << std::endl;
        return true;
    }

    // A resource's display name ("Food", "Wood", "Gold", "Stone"), from the
    // game's strings.
    const std::string& GetResourceName(int resource) const {
        static const std::string kNone;
        if (resource < 0 || resource >= static_cast<int>(ResourceType::COUNT)) {
            return kNone;
        }
        return resource_names[resource];
    }

    // How close the dat says this unit has to be to whatever it is working on
    // (a villager's build task: 0.1 tiles). Fixed point: the simulation reads it.
    Fixed32 GetWorkRange(int unit_id, Fixed32 fallback) const {
        auto it = work_range.find(unit_id);
        if (it == work_range.end() || it->second.value <= 0) return fallback;
        return it->second;
    }

    // A figure from the data files as fixed point, to the nearest thousandth.
    // Called only while loading. The result is a ratio of integers, so it is
    // identical on every machine.
    static Fixed32 ToFixed(double value) {
        const double scaled = value * 1000.0;
        const int thousandths = static_cast<int>(scaled >= 0.0 ? scaled + 0.5
                                                               : scaled - 0.5);
        return Fixed32(thousandths) / Fixed32(1000);
    }

    // How long one frame of a graphic lasts, or `fallback` if unknown.
    int GetFrameMs(int graphic_id, int fallback) const {
        auto it = frame_ms.find(graphic_id);
        return (it == frame_ms.end() || it->second <= 0) ? fallback : it->second;
    }

    // Maps a scenario/RMS object name onto the dat's internal unit name.
    static std::string DatNameFor(const std::string& rms_name) {
        if (rms_name == "STONE")    return "STONM";
        if (rms_name == "FORAGE")   return "FORAG";
        if (rms_name == "OAKTREE")  return "FOAK";
        if (rms_name == "PALMTREE") return "PTREE";
        if (rms_name == "GOLD")     return "GOLDM";
        // The playable villager is the male base unit; the female base is VFBAS.
        if (rms_name == "VILLAGER") return "VMBAS";
        return rms_name;
    }

    int GetSlpIdForUnitName(const std::string& name) {
        // SLP 4479 is the gold mine drawn on the map; the dat's GOLDM idle
        // graphic is a low-resolution version.
        if (name == "GOLD") return 4479;
        return GetUnitGraphic(DatNameFor(name), "idle_graphic0").slp_id;
    }

    // One drawable of a multi-part building, positioned in tiles from the
    // building's origin. Either a unit whose graphic is drawn, or a shadow
    // graphic named directly (a building's shadow spans the whole footprint).
    struct Annex {
        int unit_id = -1;
        int slp_id = -1;    // shadow pieces name their graphic directly
        // A right-hand piece: the counterpart of this unit's graphic, so it
        // follows the unit through age upgrades. `slp_id` is the Dark Age
        // graphic, used if nothing resolves.
        int counterpart_of = -1;
        // Offset from the building's origin: the float pair for the renderer
        // (the placement ghost), the fixed pair for the simulation.
        float dx = 0.0f;
        float dy = 0.0f;
        Fixed32 fx{};
        Fixed32 fy{};
        // Symmetric buildings reuse one piece on both sides by mirroring it.
        bool flip = false;
        bool shadow = false;
        // Optional source rect within the frame, for art holding several
        // separate pieces in one image. w == 0 means the whole frame.
        int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    };

    bool LoadAnnexes(const std::string& filepath) {
        std::ifstream af(filepath);
        if (!af.is_open()) {
            std::cerr << "[ConfigManager] Failed to open annexes: " << filepath << std::endl;
            return false;
        }
        nlohmann::json annex_json;
        try {
            af >> annex_json;
        } catch (...) { return false; }

        // Call once per layer: the dat-derived seed, then the curated
        // overrides. A later layer replaces a building's piece list outright.
        for (auto it = annex_json.begin(); it != annex_json.end(); ++it) {
            if (it.key().empty() || it.key()[0] == '_') continue; // commentary
            std::vector<Annex> pieces;
            for (const auto& piece : it.value()) {
                Annex entry;
                entry.unit_id = piece.value("unit", -1);
                entry.slp_id = piece.value("slp", -1);
                entry.counterpart_of = piece.value("counterpart", -1);
                entry.dx = piece.value("dx", 0.0f);
                entry.dy = piece.value("dy", 0.0f);
                entry.fx = ToFixed(entry.dx);
                entry.fy = ToFixed(entry.dy);
                entry.flip = piece.value("flip", false);
                entry.shadow = piece.value("shadow", false);
                if (piece.contains("src") && piece["src"].size() == 4) {
                    entry.src_x = piece["src"][0].get<int>();
                    entry.src_y = piece["src"][1].get<int>();
                    entry.src_w = piece["src"][2].get<int>();
                    entry.src_h = piece["src"][3].get<int>();
                }
                pieces.push_back(entry);
            }
            if (!pieces.empty()) annexes[std::stoi(it.key())] = std::move(pieces);
        }
        std::cout << "[ConfigManager] Loaded annexes for " << annexes.size()
                  << " buildings." << std::endl;
        return true;
    }

    // The annex pieces of a building, or null when it is a single graphic.
    const std::vector<Annex>* GetAnnexes(int unit_id) const {
        auto it = annexes.find(unit_id);
        return it == annexes.end() ? nullptr : &it->second;
    }

    // The dat unit id behind an internal name, or -1.
    int GetUnitIdByName(const std::string& dat_name) const {
        auto it = unit_by_name.find(dat_name);
        if (it == unit_by_name.end()) return -1;
        auto u = units_json.find(it->second);
        if (u == units_json.end()) return -1;
        return u->value("id", -1);
    }

    // The dat's internal name for a unit -- "VMBAS", "HOUS" -- which is what its
    // graphics are looked up by. Empty if the unit is unknown.
    std::string GetUnitDatName(int unit_id) const {
        auto u = units_json.find(std::to_string(unit_id));
        if (u == units_json.end()) return std::string{};
        return u->value("name", std::string{});
    }

    // Same as GetUnitGraphic but keyed by unit id, for annex lookups.
    GraphicInfo GetUnitGraphicById(int unit_id, const std::string& field) const {
        auto u = units_json.find(std::to_string(unit_id));
        if (u == units_json.end()) return GraphicInfo{};
        return GetGraphic(u->value(field, -1));
    }

    // The dat name for a building the simulation knows about.
    static const char* DatNameForBuilding(BuildingType type) {
        switch (type) {
        case BuildingType::TownCenter: return "RTWC";
        // The dat's names for the two drop-off camps are not the obvious
        // abbreviations: the lumber camp is SMIL and the mining camp MINE.
        case BuildingType::LumberCamp: return "SMIL";
        case BuildingType::MiningCamp: return "MINE";
        case BuildingType::Mill:       return "MILL";
        case BuildingType::Barracks:   return "BRKS";
        case BuildingType::House:      return "HOUS";
        case BuildingType::Farm:       return "FARM";
        default:                       return "";
        }
    }

    // Maps a graphic onto its separate shadow graphic: trees, buildings and
    // missiles each have a companion SLP made only of shadow pixels. Produced
    // by tools/data/extract_slp_shadows.py.
    bool LoadShadowMap(const std::string& filepath) {
        std::ifstream sf(filepath);
        if (!sf.is_open()) {
            std::cerr << "[ConfigManager] Failed to open shadow map: " << filepath << std::endl;
            return false;
        }
        nlohmann::json shadow_json;
        try {
            sf >> shadow_json;
        } catch (...) { return false; }
        for (auto it = shadow_json.begin(); it != shadow_json.end(); ++it) {
            shadow_slp_by_slp[std::stoi(it.key())] = it.value().get<int>();
        }
        std::cout << "[ConfigManager] Loaded " << shadow_slp_by_slp.size()
                  << " shadow graphics." << std::endl;
        return true;
    }

    // The shadow graphic's SLP id for a graphic, or -1 if it has none.
    int GetShadowSlp(int slp_id) const {
        auto it = shadow_slp_by_slp.find(slp_id);
        return it == shadow_slp_by_slp.end() ? -1 : it->second;
    }

    // How a unit appears in the interface, and the numbers the stat panel
    // prints. Produced by tools/data/extract_interface_data.py.
    struct UnitInterface {
        int icon = -1;             // frame in the unit or building icon sheet
        int button = -1;           // 1-based slot in the 5x3 command grid
        int train_location = -1;   // unit id of whatever produces this
        int train_time = 0;
        int population = 0;
        int costs[static_cast<int>(ResourceType::COUNT)] = {0, 0, 0, 0};
        int hp = 0;
        int attack = 0;
        int melee_armour = 0;
        int pierce_armour = 0;
        int range = 0;
        int line_of_sight = 0;
        // Which command page the button belongs to. The dat gives economic
        // buildings kind 2 and military buildings kind 10, which is how a House
        // and a Barracks can both sit on button 1 without colliding.
        int interface_kind = 0;
        // How many tiles across the building stands, or 0 for anything that
        // does not occupy ground.
        int footprint = 0;
        // The graphic worn while it is being built; its frames step with health.
        int construction_graphic = -1;
        // What it becomes when it dies: for a building, its rubble.
        int dead_unit = -1;
        // Which buildings this unit may hand a load to, by unit id (a
        // lumberjack lists the Town Centre and the Lumber Camp). This is what
        // says which drop site accepts which resource.
        std::vector<int> drop_sites;
        // Resources per second, from the dat: a lumberjack 0.39, a forager
        // 0.31. Zero for anything that does not gather. Fixed point.
        Fixed32 work_rate{};
        // How much of a resource it can shoulder before it must walk a load
        // home: ten for a villager, thirty-five for a hunter.
        int carry_capacity = 0;
        // What this unit holds for the taking: a farm's food, a house's five
        // of population headroom. -1 when it holds nothing. The resource id is
        // the dat's own, which runs wider than ResourceType (4 is population
        // headroom), so check it before treating it as a gatherable.
        int stores_resource = -1;
        int stores_amount = 0;
        // The dat's unit class. 70 is a living unit, 10 a static object; only
        // map objects carry it here, where it decides whether they have hit
        // points.
        int unit_type = 0;
        // Its gameplay class. 4 is a villager.
        int unit_class = -1;
        // Population headroom it provides while standing: 5 for a Town Centre
        // or a House, 20 for a Castle. Zero for everything else.
        int headroom = 0;
        // How it is marked when selected, from the dat. The outline's radii are
        // in tiles: a round one (obstruction type 5, units) is the ellipse a
        // circle that size makes on the ground, a square one the diamond of a
        // building's footprint. The hit point bar floats `hp_bar_height` half
        // tiles above the outline's top corner, and the selection effect says
        // whether it is shown at all (0, 1 and 5 show it). Presentation only.
        float outline_x = 0.2f;
        float outline_y = 0.2f;
        float hp_bar_height = 2.0f;
        int selection_effect = 0;
        int obstruction = 5;
        // From LANGUAGE.DLL. `help` keeps its markup: <b> toggles bold and
        // <cost> stands in for the resource cost, both resolved where the
        // text is drawn.
        std::string name;
        std::string help;
    };
    static constexpr int kInterfaceEconomic = 2;
    static constexpr int kInterfaceMilitary = 10;

    bool LoadInterfaceData(const std::string& filepath) {
        std::ifstream inf(filepath);
        if (!inf.is_open()) {
            std::cerr << "[ConfigManager] Failed to open interface data: "
                      << filepath << std::endl;
            return false;
        }
        nlohmann::json data;
        try {
            inf >> data;
        } catch (...) { return false; }

        const auto& slps = data["icon_slp"];
        icon_slp_unit = slps.value("unit", 0);
        icon_slp_building = slps.value("building", 0);
        icon_slp_command = slps.value("command", 0);
        icon_slp_research = slps.value("research", 0);
        hud_slp_base = data["hud_slp_base"].value("1280x1024", 0);
        for (const auto& civ : data["civs"]) {
            civ_names.push_back(civ.value("name", std::string{}));
        }
        for (auto it = data["labels"].begin(); it != data["labels"].end(); ++it) {
            labels[it.key()] = it.value().get<std::string>();
        }
        int resource = 0;
        for (const auto& name : data["resource_names"]) {
            if (resource < static_cast<int>(ResourceType::COUNT)) {
                resource_names[resource++] = name.get<std::string>();
            }
        }

        for (auto it = data["units"].begin(); it != data["units"].end(); ++it) {
            UnitInterface ui;
            const auto& v = it.value();
            ui.icon = v.value("icon", -1);
            ui.button = v.value("button", -1);
            ui.train_location = v.value("train_loc", -1);
            ui.train_time = v.value("train_time", 0);
            ui.population = v.value("population", 0);
            ui.hp = v.value("hp", 0);
            ui.attack = v.value("attack", 0);
            ui.melee_armour = v.value("melee_armour", 0);
            ui.pierce_armour = v.value("pierce_armour", 0);
            ui.range = v.value("range", 0);
            ui.line_of_sight = v.value("line_of_sight", 0);
            ui.interface_kind = v.value("interface_kind", 0);
            ui.footprint = v.value("footprint", 0);
            ui.construction_graphic = v.value("construction_graphic", -1);
            ui.dead_unit = v.value("dead_unit", -1);
            ui.work_rate = ToFixed(v.value("work_rate", 0.0));
            ui.carry_capacity = v.value("carry_capacity", 0);
            ui.unit_type = v.value("unit_type", 0);
            ui.unit_class = v.value("unit_class", -1);
            ui.headroom = v.value("headroom", 0);
            if (v.contains("outline") && v["outline"].size() == 2) {
                ui.outline_x = v["outline"][0].get<float>();
                ui.outline_y = v["outline"][1].get<float>();
            }
            ui.hp_bar_height = v.value("hp_bar_height", 2.0f);
            ui.selection_effect = v.value("selection_effect", 0);
            ui.obstruction = v.value("obstruction", 5);
            if (v.contains("stores") && v["stores"].size() == 2) {
                ui.stores_resource = v["stores"][0].get<int>();
                ui.stores_amount = v["stores"][1].get<int>();
            }
            if (v.contains("drop_sites")) {
                for (const auto& site : v["drop_sites"]) {
                    ui.drop_sites.push_back(site.get<int>());
                }
            }
            ui.name = v.value("name", std::string{});
            ui.help = v.value("help", std::string{});
            for (const auto& cost : v["costs"]) {
                const int resource = cost[0].get<int>();
                if (resource >= 0 && resource < static_cast<int>(ResourceType::COUNT)) {
                    ui.costs[resource] = cost[1].get<int>();
                }
            }
            const int unit_id = std::stoi(it.key());
            unit_interface[unit_id] = ui;
            if (ui.train_location >= 0) produced_at[ui.train_location].push_back(unit_id);
        }
        // Ordered by command slot so a production menu comes out the same way
        // every run, whatever order the JSON happened to be walked in.
        for (auto& entry : produced_at) {
            auto& ids = entry.second;
            std::sort(ids.begin(), ids.end(), [this](int a, int b) {
                const int ba = unit_interface[a].button;
                const int bb = unit_interface[b].button;
                return ba != bb ? ba < bb : a < b;
            });
        }
        std::cout << "[ConfigManager] Loaded interface data for "
                  << unit_interface.size() << " units." << std::endl;
        return true;
    }

    // What a building produces, ordered by command slot. Empty for anything
    // that produces nothing.
    const std::vector<int>& GetProducedAt(int location_unit_id) const {
        static const std::vector<int> kNone;
        auto it = produced_at.find(location_unit_id);
        return it == produced_at.end() ? kNone : it->second;
    }

    // A building's art for one age and architecture set, resolved by name from
    // the graphic table (the unit's standing graphic is wrong for several
    // buildings). See tools/data/extract_building_art.py.
    struct BuildingArt {
        int body = -1;      // graphic id of the building itself
        int shadow = -1;    // its companion shadow graphic, or -1
        int damage[3] = {-1, -1, -1};
        bool Valid() const { return body >= 0; }
    };

    bool LoadBuildingArt(const std::string& filepath) {
        std::ifstream bf(filepath);
        if (!bf.is_open()) {
            std::cerr << "[ConfigManager] Failed to open building art: "
                      << filepath << std::endl;
            return false;
        }
        nlohmann::json data;
        try { bf >> data; } catch (...) { return false; }

        for (auto civ = data["civ_arch"].begin();
             civ != data["civ_arch"].end(); ++civ) {
            civ_architecture[std::stoi(civ.key())] = civ.value().get<std::string>();
        }
        for (auto unit = data["art"].begin(); unit != data["art"].end(); ++unit) {
            const int unit_id = std::stoi(unit.key());
            for (auto age = unit.value().begin(); age != unit.value().end(); ++age) {
                for (auto arch = age.value().begin();
                     arch != age.value().end(); ++arch) {
                    BuildingArt art;
                    art.body = arch.value().value("body", -1);
                    art.shadow = arch.value().value("shadow", -1);
                    int index = 0;
                    if (arch.value().contains("damage")) {
                        for (const auto& stage : arch.value()["damage"]) {
                            if (index < 3) art.damage[index++] = stage.get<int>();
                        }
                    }
                    building_art[Key(unit_id, std::stoi(age.key()), arch.key())] = art;
                }
            }
        }
        std::cout << "[ConfigManager] Loaded art for " << data["art"].size()
                  << " buildings." << std::endl;
        return true;
    }

    // The architecture set a civilisation builds in, as a one-letter suffix.
    const std::string& CivArchitecture(int civ_id) const {
        static const std::string kGeneric;
        auto it = civ_architecture.find(civ_id);
        return it == civ_architecture.end() ? kGeneric : it->second;
    }

    // A building's art. Falls back from the civ's own architecture set to the
    // generic one, which is what a building that looks the same for everyone
    // has, and from a later age to the one before it.
    BuildingArt GetBuildingArt(int unit_id, int age, int civ_id) const {
        const std::string arch = CivArchitecture(civ_id);
        for (int try_age = age; try_age >= 1; --try_age) {
            auto it = building_art.find(Key(unit_id, try_age, arch));
            if (it != building_art.end()) return it->second;
            it = building_art.find(Key(unit_id, try_age, std::string()));
            if (it != building_art.end()) return it->second;
        }
        return BuildingArt{};
    }

    // The display name, or an empty string when the data has none for it.
    const std::string& GetUnitName(int unit_id) const {
        static const std::string kNone;
        const UnitInterface* ui = GetUnitInterface(unit_id);
        return ui ? ui->name : kNone;
    }

    // The numbers a help template can name, filled from whatever the caller
    // has (a player's own rules, after technologies).
    struct HelpValues {
        int costs[static_cast<int>(ResourceType::COUNT)] = {};
        int hp = 0;
        int attack = 0;
        int melee_armour = 0;
        int pierce_armour = 0;
        int range = 0;
    };

    std::string ExpandHelp(const UnitInterface& ui) const {
        HelpValues values;
        for (int r = 0; r < static_cast<int>(ResourceType::COUNT); ++r) values.costs[r] = ui.costs[r];
        values.hp = ui.hp;
        values.attack = ui.attack;
        values.melee_armour = ui.melee_armour;
        values.pierce_armour = ui.pierce_armour;
        values.range = ui.range;
        return ExpandHelp(ui.help, values);
    }

    // Expands a help string template: `<b>` and `<i>` toggle bold and italic
    // and are dropped unless `keep_markup` is set; `<cost>` becomes the cost
    // line; `<hp>`, `<attack>`, `<armor>`, `<piercearmor>` and `<range>`
    // become labelled numbers. Labels and resource names come from
    // LANGUAGE.DLL.
    std::string ExpandHelp(const std::string& help, const HelpValues& v,
                           bool keep_markup = false) const {
        std::string out;
        out.reserve(help.size() + 64);

        for (size_t i = 0; i < help.size();) {
            if (help[i] != '<') {
                out += help[i++];
                continue;
            }
            const size_t close = help.find('>', i);
            if (close == std::string::npos) { out += help[i++]; continue; }
            const std::string tag = help.substr(i + 1, close - i - 1);
            i = close + 1;

            if (tag == "b" || tag == "i") {
                if (keep_markup) out += "<" + tag + ">";
                continue;
            }
            if (tag == "cost") {
                out += LabelFor("cost");
                bool first = true;
                for (int r = 0; r < static_cast<int>(ResourceType::COUNT); ++r) {
                    if (v.costs[r] <= 0) continue;
                    if (!first) out += ", ";
                    first = false;
                    out += std::to_string(v.costs[r]) + " " + resource_names[r];
                }
                continue;
            }
            if (tag == "hp")          { out += LabelFor(tag) + std::to_string(v.hp); continue; }
            if (tag == "attack")      { out += LabelFor(tag) + std::to_string(v.attack); continue; }
            if (tag == "armor")       { out += LabelFor(tag) + std::to_string(v.melee_armour); continue; }
            if (tag == "piercearmor") { out += LabelFor(tag) + std::to_string(v.pierce_armour); continue; }
            if (tag == "range")       { out += LabelFor(tag) + std::to_string(v.range); continue; }
            // Any other placeholder is dropped.
        }

        // Dropping the markup leaves the spaces that surrounded it, so
        // "Build <b> House<b>" would come out double-spaced.
        std::string tidy;
        tidy.reserve(out.size());
        for (char c : out) {
            if (c == ' ' && !tidy.empty() && tidy.back() == ' ') continue;
            tidy += c;
        }
        return tidy;
    }

    const std::string& LabelFor(const std::string& key) const {
        static const std::string kNone;
        auto it = labels.find(key);
        return it == labels.end() ? kNone : it->second;
    }

    // Terrain art and the names the random map scripts use for it. Produced by
    // tools/data/extract_terrain_data.py.
    struct TerrainInfo {
        uint32_t slp_id = 0;
        // Cells of the ground's tiling pattern, always a perfect square.
        int frame_count = 0;
        // The unit that grows on every tile of this ground, or -1. What makes
        // forest ground a forest is the tree the dat's terrain table puts on it.
        int tree_unit_id = -1;
        // The terrain's colour on the minimap, as an index into the game's
        // palette rather than an RGB triple.
        int minimap_index = 0;
        // The dat's three minimap shades, brightest first: ground sloping
        // toward the light, flat, and sloping away.
        int minimap_shades[3] = {0, 0, 0};
        // Its id in the dat, which is also the number a map script uses.
        int dat_id = -1;
        // Where two grounds meet, the higher priority draws over the lower;
        // the blend type is the class of transition each makes -- land, farm,
        // beach, water, shallows -- and the pair picks the mask set.
        int blend_priority = 0;
        int blend_type = 0;
    };

    bool LoadTerrainData(const std::string& filepath) {
        std::ifstream tf(filepath);
        if (!tf.is_open()) {
            std::cerr << "[ConfigManager] Failed to open terrain data: "
                      << filepath << std::endl;
            return false;
        }
        nlohmann::json data;
        try { tf >> data; } catch (...) { return false; }

        const auto& slps = data["slps"];
        for (auto it = data["engine"].begin(); it != data["engine"].end(); ++it) {
            TerrainInfo info;
            info.slp_id = it.value().value("slp", 0);
            const auto frames = slps.find(std::to_string(info.slp_id));
            if (frames == slps.end()) continue;
            info.frame_count = frames->value("frames", 0);
            info.tree_unit_id = it.value().value("tree", -1);
            info.minimap_index = it.value().value("minimap", 0);
            for (int k = 0; k < 3; ++k) info.minimap_shades[k] = info.minimap_index;
            if (it.value().contains("minimap_shades") && it.value()["minimap_shades"].size() == 3) {
                for (int k = 0; k < 3; ++k) {
                    info.minimap_shades[k] = it.value()["minimap_shades"][k].get<int>();
                }
            }
            info.dat_id = it.value().value("terrain", -1);
            info.blend_priority = it.value().value("blend_priority", 0);
            info.blend_type = it.value().value("blend_type", 0);
            terrain_by_name[it.key()] = info;

            // Also indexed by the enum, so the render loop never looks a
            // terrain up by string, and by the dat's terrain id, which is what
            // a map script names.
            for (int t = 0; t < static_cast<int>(TerrainType::COUNT); ++t) {
                if (it.key() != NameForTerrain(static_cast<TerrainType>(t))) continue;
                terrain_by_type[t] = info;
                if (info.dat_id >= 0) {
                    terrain_type_by_rms_id.emplace(info.dat_id,
                                                   static_cast<TerrainType>(t));
                }
                break;
            }
        }
        for (auto section = data["constants"].begin();
             section != data["constants"].end(); ++section) {
            for (auto it = section.value().begin(); it != section.value().end(); ++it) {
                const int value = it.value().get<int>();
                rms_constants[it.key()] = value;
            }
        }

        for (const auto& base : data["player_ramps"]) {
            player_ramps.push_back(base.get<int>());
        }
        for (const auto& index : data["player_minimap"]) {
            player_minimap.push_back(index.get<int>());
        }
        for (auto it = data["scripts"].begin(); it != data["scripts"].end(); ++it) {
            rms_script_ids[it.key()] = it.value().get<uint32_t>();
        }
        for (auto it = data["object_art"].begin();
             it != data["object_art"].end(); ++it) {
            ObjectArt art;
            art.slp_id = it.value().value("slp", -1);
            art.frame_count = it.value().value("frame_count", 0);
            art.angle_count = it.value().value("angle_count", 1);
            art.frame_ms = it.value().value("frame_ms", 0);
            art.minimap_index = it.value().value("minimap", 0);
            art.unit_id = std::stoi(it.key());
            object_art[art.unit_id] = art;
        }
        for (auto it = data["gaia"].begin(); it != data["gaia"].end(); ++it) {
            GaiaObject object;
            object.resource = it.value().value("resource", -1);
            object.amount = it.value().value("amount", 0);
            gaia_objects[std::stoi(it.key())] = object;
        }

        std::cout << "[ConfigManager] Loaded " << terrain_by_name.size()
                  << " terrains, " << object_art.size() << " map objects, "
                  << rms_constants.size() << " map-script constants, "
                  << rms_script_ids.size() << " random map scripts." << std::endl;
        return true;
    }

    // The engine's name for a terrain, which is the key the extracted table
    // uses.
    static const char* NameForTerrain(TerrainType type) {
        switch (type) {
        case TerrainType::DeepWater:    return "DeepWater";
        case TerrainType::MediumWater:  return "MediumWater";
        case TerrainType::ShallowWater: return "ShallowWater";
        case TerrainType::Swamp:        return "Swamp";
        case TerrainType::Beach:        return "Beach";
        case TerrainType::Sand:         return "Sand";
        case TerrainType::Dirt1:        return "Dirt1";
        case TerrainType::Dirt2:        return "Dirt2";
        case TerrainType::Dirt3:        return "Dirt3";
        case TerrainType::Grass1:       return "Grass1";
        case TerrainType::Grass2:       return "Grass2";
        case TerrainType::Grass3:       return "Grass3";
        case TerrainType::Grass4:       return "Grass4";
        case TerrainType::Forest:       return "Forest";
        case TerrainType::Farm:         return "Farm";
        case TerrainType::FarmDead:     return "FarmDead";
        case TerrainType::FarmBuild1:   return "FarmBuild1";
        case TerrainType::FarmBuild2:   return "FarmBuild2";
        case TerrainType::FarmBuild3:   return "FarmBuild3";
        case TerrainType::Foundation:   return "Foundation";
        case TerrainType::Stone1:       return "Stone1";
        case TerrainType::Stone2:       return "Stone2";
        case TerrainType::Ice:          return "Ice";
        default:                        return "";
        }
    }

    // The art for one terrain. Indexed lookup, safe to call per tile.
    const TerrainInfo& GetTerrainArt(TerrainType type) const {
        static const TerrainInfo kNone;
        const int index = static_cast<int>(type);
        if (index < 0 || index >= static_cast<int>(TerrainType::COUNT)) return kNone;
        return terrain_by_type[index];
    }

    // The art for one terrain, by the engine's own name for it.
    const TerrainInfo* GetTerrain(const std::string& engine_name) const {
        auto it = terrain_by_name.find(engine_name);
        return it == terrain_by_name.end() ? nullptr : &it->second;
    }

    // A `#const` from random_map.def -- terrain, object, unit or building.
    int RmsConstant(const std::string& name, int fallback = -1) const {
        auto it = rms_constants.find(name);
        return it == rms_constants.end() ? fallback : it->second;
    }

    // What a map object is worth: gold, stone, forage, a tree, a huntable.
    struct GaiaObject {
        int resource = -1;   // Core::ResourceType, or -1 for nothing
        int amount = 0;
    };

    const GaiaObject* GetGaiaObject(int unit_id) const {
        auto it = gaia_objects.find(unit_id);
        return it == gaia_objects.end() ? nullptr : &it->second;
    }

    // A map object's art: the SLP its idle graphic draws, and how many frames
    // that holds. Static objects use their frames as interchangeable shapes
    // (fourteen tree silhouettes, seven arrangements of gold), picked at
    // random. Keyed by unit id: two units can share one graphic.
    struct ObjectArt {
        // Which unit this art belongs to, so a placed object can be named.
        int unit_id = -1;
        int slp_id = -1;
        // One frame per angle means the angles are interchangeable shapes to
        // pick from; more than one means the object plays an animation.
        int frame_count = 0;
        int angle_count = 0;
        int frame_ms = 0;
        int minimap_index = 0;   // 0 = not shown on the minimap
        bool Valid() const { return slp_id >= 0 && frame_count > 0; }
        bool Animated() const { return frame_count > 1; }
        int TotalFrames() const { return frame_count * std::max(1, angle_count); }
    };

    ObjectArt GetObjectArt(int unit_id) const {
        auto it = object_art.find(unit_id);
        return it == object_art.end() ? ObjectArt{} : it->second;
    }

    // Where each player's eight-entry colour ramp starts in the palette. Not a
    // fixed stride: player 5 sits at 96 and player 8 at 80.
    const std::vector<int>& PlayerRamps() const { return player_ramps; }

    // The minimap palette entry of a colour, 0-7, whoever wears it: the
    // lobby's colour boxes.
    int ColourMinimapIndex(int colour) const {
        if (colour < 0 || colour >= static_cast<int>(player_minimap.size())) return 0;
        return player_minimap[static_cast<size_t>(colour)];
    }

    // The colour each player wears, 0-7, by player number; -1 for the seat's
    // own. Set for each game from its lobby.
    void SetPlayerColours(const std::array<int, 16>& slots) { colour_slots = slots; }

    // The pure palette entry a player is drawn with where shading would only
    // muddy it: the minimap dot and the selection outline.
    int PlayerMinimapColour(int player) const {
        // The colour the player wears: its seat's own (the dat's table is
        // 0-based), or the lobby's choice.
        const int chosen = player >= 0 && player < 16 ? colour_slots[static_cast<size_t>(player)] : -1;
        const int index = chosen >= 0 ? chosen : player - 1;
        if (index < 0 || index >= static_cast<int>(player_minimap.size())) return 0;
        return player_minimap[index];
    }

    // The archive entry holding a random map script, or 0. Named as
    // random_map.def lists them: "Arabia", "BlackForest", "TeamIslands".
    uint32_t RmsScriptId(const std::string& script_name) const {
        auto it = rms_script_ids.find(script_name);
        return it == rms_script_ids.end() ? 0u : it->second;
    }

    const std::unordered_map<std::string, uint32_t>& RmsScripts() const {
        return rms_script_ids;
    }

    // The engine terrain a map script's terrain id refers to. Returns false
    // when the script names ground the engine has no art for.
    bool TerrainForRmsId(int rms_id, TerrainType& out) const {
        auto it = terrain_type_by_rms_id.find(rms_id);
        if (it == terrain_type_by_rms_id.end()) return false;
        out = it->second;
        return true;
    }

    // Buildings and units draw their icons from separate sheets. The building
    // sheet's are the dat's buildings (type 80), all but the siege engines
    // that pack, which the dat also makes type 80 and whose icons are units'.
    bool UsesBuildingIcons(int unit_id) const {
        auto u = units_json.find(std::to_string(unit_id));
        if (u == units_json.end() || u->value("type", 0) != kDatTypeBuilding) return false;
        const int cls = u->value("class", -1);
        return cls != kDatClassPackedUnit && cls != kDatClassUnpackedSiege;
    }

    const UnitInterface* GetUnitInterface(int unit_id) const {
        auto it = unit_interface.find(unit_id);
        return it == unit_interface.end() ? nullptr : &it->second;
    }

    uint32_t UnitIconSlp() const { return icon_slp_unit; }
    uint32_t BuildingIconSlp() const { return icon_slp_building; }
    uint32_t CommandIconSlp() const { return icon_slp_command; }
    uint32_t ResearchIconSlp() const { return icon_slp_research; }

    // The panel art for a civilisation: one full-screen set per civ, in the
    // dat's civ order.
    uint32_t HudSlpForCiv(int civ_id) const {
        if (hud_slp_base == 0 || civ_id < 1 ||
            civ_id > static_cast<int>(civ_names.size())) {
            return hud_slp_base;
        }
        return hud_slp_base + static_cast<uint32_t>(civ_id - 1);
    }

    const std::string& GetCivName(int civ_id) const {
        static const std::string kNone;
        if (civ_id < 1 || civ_id > static_cast<int>(civ_names.size())) return kNone;
        return civ_names[static_cast<size_t>(civ_id - 1)];
    }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

public:
    static bool ParseInt(const std::string& s, size_t begin, size_t end, int& out) {
        if (begin >= end) return false;
        int value = 0;
        for (size_t i = begin; i < end; ++i) {
            const char c = s[i];
            if (c < '0' || c > '9') return false;
            value = value * 10 + (c - '0');
        }
        out = value;
        return true;
    }

    std::unordered_map<int, int> shadow_slp_by_slp;
    std::unordered_map<int, std::vector<Annex>> annexes;
    // The dat's unit type for a building, and its classes for a siege engine
    // packed and set up.
    static constexpr int kDatTypeBuilding = 80;
    static constexpr int kDatClassPackedUnit = 51;
    static constexpr int kDatClassUnpackedSiege = 54;

    std::unordered_map<int, UnitInterface> unit_interface;
    std::unordered_map<int, std::vector<int>> produced_at;
    TerrainInfo terrain_by_type[static_cast<int>(TerrainType::COUNT)];
    std::unordered_map<std::string, TerrainInfo> terrain_by_name;
    std::unordered_map<std::string, int> rms_constants;
    std::unordered_map<int, TerrainType> terrain_type_by_rms_id;
    std::unordered_map<std::string, uint32_t> rms_script_ids;
    // Building art, keyed by unit, age and architecture set.
    static std::string Key(int unit_id, int age, const std::string& arch) {
        return std::to_string(unit_id) + ":" + std::to_string(age) + ":" + arch;
    }
    std::unordered_map<std::string, BuildingArt> building_art;
    std::unordered_map<int, std::string> civ_architecture;

    std::unordered_map<int, GaiaObject> gaia_objects;
    std::unordered_map<int, ObjectArt> object_art;
    std::vector<int> player_ramps;
    std::array<int, 16> colour_slots{-1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<int> player_minimap;
    uint32_t icon_slp_unit = 0;
    uint32_t icon_slp_building = 0;
    uint32_t icon_slp_command = 0;
    uint32_t icon_slp_research = 0;
    uint32_t hud_slp_base = 0;
    std::vector<std::string> civ_names;
    std::unordered_map<std::string, std::string> labels;
    std::string resource_names[static_cast<int>(ResourceType::COUNT)];
    std::unordered_map<int, int> attack_graphic;
    std::unordered_map<int, int> work_graphic;
    std::unordered_map<int, int> carry_graphic;
    std::unordered_map<int, Fixed32> work_range;
    std::unordered_map<int, int> frame_ms;
    // dat name -> json key, so lookups are not a linear scan of 720 units.
    std::unordered_map<std::string, std::string> unit_by_name;
    std::unordered_map<std::string, std::string> graphic_by_name;
    nlohmann::json units_json;
    nlohmann::json graphics_json;
};

} // namespace Core
