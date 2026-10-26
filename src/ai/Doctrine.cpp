#include "ai/Doctrine.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Types.h"

namespace AI::Doctrine {

namespace {

struct Shares {
    Need need = Need::None;
    int age = 0;
    bool and_later = false;
    int civilians_below = -1;
    int civilians_at_least = -1;
    // -1: the rule leaves it as it was.
    int share[4] = {-1, -1, -1, -1};
};

std::map<int, std::array<int, 3>> g_villagers;   // by cap: dark, feudal, castle
std::vector<Shares> g_shares;
std::vector<std::string> g_fishing_maps;
int g_dock_water = 0;
std::map<int, std::array<int, 2>> g_fishing_ships;   // by cap: Dark Age, later

// A map script's name as the AI's #load-if-defined names it: letters only,
// upper case -- "TeamIslands" and "TEAM-ISLANDS" alike are TEAMISLANDS.
std::string Plain(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c >= 'a' && c <= 'z') out.push_back(static_cast<char>(c - 'a' + 'A'));
        else if (c >= 'A' && c <= 'Z') out.push_back(c);
    }
    return out;
}

int Index(Core::ResourceType type) { return static_cast<int>(type); }

} // namespace

bool Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[AI] Cannot open " << path << std::endl;
        return false;
    }
    nlohmann::json data;
    in >> data;
    for (auto it = data["villagers"].begin(); it != data["villagers"].end(); ++it) {
        const auto& v = it.value();
        g_villagers[std::stoi(it.key())] = {v[0].get<int>(), v[1].get<int>(), v[2].get<int>()};
    }
    for (const auto& rule : data["gatherers"]) {
        Shares s;
        const std::string need = rule.value("need", std::string("NO"));
        s.need = need == "FOOD" ? Need::Food : need == "WOOD" ? Need::Wood
               : need == "GOLD" ? Need::Gold : need == "STONE" ? Need::Stone : Need::None;
        s.age = rule.value("age", 0);
        s.and_later = rule.value("and_later", false);
        s.civilians_below = rule.value("civilians_below", -1);
        s.civilians_at_least = rule.value("civilians_at_least", -1);
        s.share[Index(Core::ResourceType::Wood)] = rule.value("wood", -1);
        s.share[Index(Core::ResourceType::Food)] = rule.value("food", -1);
        s.share[Index(Core::ResourceType::Gold)] = rule.value("gold", -1);
        s.share[Index(Core::ResourceType::Stone)] = rule.value("stone", -1);
        g_shares.push_back(s);
    }
    if (data.contains("fishing")) {
        const auto& fishing = data["fishing"];
        for (const auto& m : fishing["maps"]) g_fishing_maps.push_back(Plain(m.get<std::string>()));
        g_dock_water = fishing.value("dock_water_tiles", 0);
        for (auto it = fishing["fishing_ships"].begin(); it != fishing["fishing_ships"].end(); ++it) {
            g_fishing_ships[std::stoi(it.key())] = {it.value()[0].get<int>(), it.value()[1].get<int>()};
        }
    }
    return true;
}

bool FishingMap(const std::string& script) {
    const std::string plain = Plain(script);
    return std::find(g_fishing_maps.begin(), g_fishing_maps.end(), plain) != g_fishing_maps.end();
}

int DockWaterTiles() { return g_dock_water; }

int FishingShips(int population_cap, int age) {
    if (g_fishing_ships.empty()) return 0;
    auto it = g_fishing_ships.upper_bound(population_cap);
    if (it != g_fishing_ships.begin()) --it;
    return it->second[age > 0 ? 1 : 0];
}

int VillagerTarget(int population_cap, int age) {
    if (g_villagers.empty()) return 25;
    // The largest cap in the table that does not exceed the lobby's.
    auto it = g_villagers.upper_bound(population_cap);
    if (it != g_villagers.begin()) --it;
    return it->second[static_cast<size_t>(std::clamp(age, 0, 2))];
}

std::array<int, 4> GathererShares(int age, int civilians, Need need) {
    // The rules run in order, each setting what it names and leaving the
    // rest: the shares are whatever the last matching rule left.
    std::array<int, 4> out = {};
    out[static_cast<size_t>(Index(Core::ResourceType::Food))] = 100;
    // A need the age has no rule for is no need at all.
    bool known = false;
    for (const Shares& s : g_shares) {
        const bool age_ok = s.and_later ? age >= s.age : age == s.age;
        if (age_ok && s.need == need) known = true;
    }
    if (!known) need = Need::None;
    for (const Shares& s : g_shares) {
        const bool age_ok = s.and_later ? age >= s.age : age == s.age;
        if (!age_ok || s.need != need) continue;
        if (s.civilians_below >= 0 && civilians >= s.civilians_below) continue;
        if (s.civilians_at_least >= 0 && civilians < s.civilians_at_least) continue;
        for (int r = 0; r < 4; ++r) {
            if (s.share[r] >= 0) out[static_cast<size_t>(r)] = s.share[r];
        }
    }
    return out;
}

} // namespace AI::Doctrine
