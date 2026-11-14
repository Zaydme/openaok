#include "core/Worldcraft.h"

#include <fstream>
#include <array>
#include <map>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "core/Treeman.h"
#include "core/TrojanHorse.h"
#include "core/Gorynych.h"

namespace Core::worldcraft
{
  namespace
  {
    PlantRules loaded;
    std::array<WorkRules, 7> earthRules;
    std::map<std::string, std::string> strings;

    nlohmann::json read(const std::filesystem::path& path)
    {
      std::ifstream input(path);
      if (!input) throw std::runtime_error("cannot open " + path.string());
      return nlohmann::json::parse(input);
    }

    int integer(const nlohmann::json& json, const char* key, int low, int high)
    {
      const auto& value = json.at(key);
      if (!value.is_number_integer()) throw std::runtime_error(std::string(key) + " must be an integer");
      const auto number = value.get<int64_t>();
      if (number < low || number > high) throw std::runtime_error(std::string(key) + " is out of range");
      return static_cast<int>(number);
    }
  }

  bool load(const std::filesystem::path& root, std::string& error)
  {
    error.clear();
    try
    {
      const auto manifest = read(root / "manifest.json");
      if (manifest.at("schema") != 1 || manifest.at("id") != "worldcraft" ||
          manifest.at("version") != "0.4.0" || manifest.at("rules") != "rules.json" ||
          manifest.at("graphics") != "graphics.json" || manifest.at("strings") != "strings.ru.json")
        throw std::runtime_error("unsupported worldcraft manifest");
      const auto allRules = read(root / "rules.json");
      const auto rules = allRules.at("plant_tree");
      std::array<WorkRules, 7> earthCandidate;
      const std::array<const char*, 7> names{"raise_ground", "dig_ditch", "restore_ground", "dig_channel", "dig_lake", "dig_river", "dig_water_moat"};
      for (std::size_t i = 0; i < names.size(); ++i)
      {
        const auto& rule = allRules.at(names[i]);
        earthCandidate[i] = {integer(rule, "wood_cost", 0, 1000), integer(rule, "work_ms", 16, 300000)};
      }
      const auto art = read(root / "graphics.json").at("plant_tree");
      PlantRules candidate;
      candidate.woodCost = integer(rules, "wood_cost", 0, 10000);
      candidate.workMs = integer(rules, "work_ms", 16, 3600000);
      candidate.growthMs = integer(rules, "growth_ms", 16, 3600000);
      candidate.treeUnit = integer(rules, "tree_unit", 0, 32767);
      candidate.siteUnit = integer(art, "site_unit", 0, 32767);
      candidate.growingUnit = integer(art, "growing_unit", 0, 32767);
      candidate.buttonIcon = integer(art, "button_command_icon", 0, 63);
      const auto words = read(root / "strings.ru.json");
      std::map<std::string, std::string> labels;
      for (const auto* key : {"plant", "plant_help", "line", "area", "cancel", "cancel_help", "working", "growing", "choose_site",
          "raise_ground", "dig_ditch", "restore_ground", "dig_channel", "dig_lake", "earthwork_help", "line_shape", "area_shape",
          "earthwork_menu", "hill_height", "mountain_height", "high_mountain_height", "height_help",
          "dig_river", "dig_water_moat", "waterwork_menu", "waterwork_help", "water_width"})
      {
        const auto value = words.at(key).get<std::string>();
        if (value.empty() || value.size() > 4096) throw std::runtime_error(std::string("invalid text: ") + key);
        labels.emplace(key, value);
      }
      loaded = candidate;
      earthRules = earthCandidate;
      strings = std::move(labels);
      return true;
    }
    catch (const std::exception& exception)
    {
      error = exception.what();
      return false;
    }
  }

  bool validateArt(std::string& error)
  {
    const auto* tree = Rules::Unit(loaded.treeUnit);
    constexpr int treeClass = 15;
    if (tree == nullptr || tree->cls != treeClass)
    {
      error = "worldcraft tree_unit must name a tree";
      return false;
    }
    int wood = 0;
    for (int i = 0; i < tree->storage_count; ++i)
      if (tree->storage[i].mode == 0 && tree->storage[i].resource == Rules::Res::Wood)
        wood += tree->storage[i].amount.ToInt();
    if (wood <= 0)
    {
      error = "worldcraft tree_unit must supply wood";
      return false;
    }
    for (const int unit : {loaded.siteUnit, loaded.growingUnit, loaded.treeUnit})
    {
      const auto& config = ConfigManager::Get();
      if (Rules::Unit(unit) == nullptr ||
          !config.GetUnitGraphicById(unit, "idle_graphic0").Valid())
      {
        error = "worldcraft art is unavailable for unit " + std::to_string(unit);
        return false;
      }
    }
    return true;
  }

  const PlantRules& plantRules()
  {
    return loaded;
  }

  const std::string& text(const std::string& key)
  {
    return strings.at(key);
  }

  int builtUnit(WorldJobKind kind)
  {
    if (kind == WorldJobKind::GrowGorynych) return gorynych::unitId;
    return kind == WorldJobKind::BuildTransport ? trojan_horse::unitId : treeman::unitId;
  }

  int builtPopulation(WorldJobKind kind)
  {
    if (kind == WorldJobKind::GrowGorynych) return gorynych::settings().m_population;
    return kind == WorldJobKind::BuildTransport ? trojan_horse::settings().m_population : treeman::settings().population;
  }

  WorkRules workRules(WorldJobKind kind)
  {
    if (kind == WorldJobKind::GrowGorynych)
    {
      const auto& unit = gorynych::settings();
      return {unit.m_woodCost, unit.m_workMs, unit.m_goldCost};
    }
    if (kind == WorldJobKind::GrowTreeman)
    {
      const auto& unit = treeman::settings();
      return {unit.woodCost, unit.workMs, unit.goldCost};
    }
    if (kind == WorldJobKind::BuildTransport)
    {
      const auto& unit = trojan_horse::settings();
      return {unit.m_woodCost, unit.m_workMs, unit.m_goldCost};
    }
    if (kind == WorldJobKind::PlantTree) return {loaded.woodCost, loaded.workMs};
    if (!isEarthwork(kind)) throw std::invalid_argument("invalid worldcraft job kind");
    if (isWaterwork(kind)) return earthRules.at(kind == WorldJobKind::DigRiver ? 5 : 6);
    return earthRules.at(static_cast<std::size_t>(kind) - static_cast<std::size_t>(WorldJobKind::RaiseGround));
  }

  const std::string& jobName(WorldJobKind kind)
  {
    switch (kind)
    {
    case WorldJobKind::GrowGorynych: return gorynych::settings().m_grow;
    case WorldJobKind::BuildTransport: return trojan_horse::settings().m_build;
    case WorldJobKind::GrowTreeman: return treeman::settings().grow;
    case WorldJobKind::RaiseGround: return text("raise_ground");
    case WorldJobKind::DigDitch: return text("dig_ditch");
    case WorldJobKind::RestoreGround: return text("restore_ground");
    case WorldJobKind::DigChannel: return text("dig_channel");
    case WorldJobKind::DigLake: return text("dig_lake");
    case WorldJobKind::DigRiver: return text("dig_river");
    case WorldJobKind::DigWaterMoat: return text("dig_water_moat");
    default: return text("working");
    }
  }

  std::string earthworkHelp(WorldJobKind kind)
  {
    auto result = jobName(kind) + ". " + text(isWaterwork(kind) ? "waterwork_help" : "earthwork_help");
    const auto rules = workRules(kind);
    for (const auto& [key, value] : {std::pair<std::string, int>{"{cost}", rules.woodCost},
        {"{work}", (rules.workMs + 999) / 1000}, {"{limit}", siteLimit}})
      for (auto at = result.find(key); at != std::string::npos; at = result.find(key, at))
        result.replace(at, key.size(), std::to_string(value));
    return result;
  }

  const std::string& heightName(int height)
  {
    return text(height == hillHeight ? "hill_height" : height == highMountainHeight ? "high_mountain_height" : "mountain_height");
  }

  std::string plantHelp()
  {
    auto result = text("plant_help");
    const auto replace = [&result](const char* token, int value)
    {
      const std::string key(token);
      for (auto at = result.find(key); at != std::string::npos; at = result.find(key, at))
        result.replace(at, key.size(), std::to_string(value));
    };
    replace("{cost}", loaded.woodCost);
    replace("{work}", (loaded.workMs + 999) / 1000);
    replace("{growth}", (loaded.growthMs + 999) / 1000);
    replace("{limit}", siteLimit);
    return result;
  }
}
