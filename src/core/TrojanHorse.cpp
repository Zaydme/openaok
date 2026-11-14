#include "core/TrojanHorse.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "core/Treeman.h"
#include "core/Gorynych.h"
#include "hal/SlpFile.h"

namespace Core::trojan_horse
{
  namespace
  {
    Settings loaded;

    nlohmann::json read(const std::filesystem::path& path)
    {
      std::ifstream input(path);
      if (!input) throw std::runtime_error("cannot open " + path.string());
      return nlohmann::json::parse(input);
    }

    int integer(const nlohmann::json& data, const char* key, int low, int high)
    {
      const auto& value = data.at(key);
      if (!value.is_number_integer()) throw std::runtime_error(std::string(key) + " must be an integer");
      const auto number = value.get<int64_t>();
      if (number < low || number > high) throw std::runtime_error(std::string(key) + " is out of range");
      return static_cast<int>(number);
    }

    std::string label(const nlohmann::json& data, const char* key)
    {
      auto text = data.at(key).get<std::string>();
      if (text.empty() || text.size() > 4096) throw std::runtime_error(std::string("invalid text: ") + key);
      return text;
    }

    void replace(std::string& text, const char* token, int number)
    {
      const std::string key(token), value = std::to_string(number);
      for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size()))
        text.replace(at, key.size(), value);
    }
  }

  bool load(const std::filesystem::path& root, std::string& error)
  {
    error.clear();
    try
    {
      const auto data = read(root / "unit.json");
      if (data.at("schema") != 1 || data.at("unit_id") != unitId || data.at("corpse_id") != corpseId)
        throw std::runtime_error("unsupported Trojan horse unit schema or ids");
      auto& config = ConfigManager::Get();
      if (Rules::Unit(unitId) || Rules::Unit(corpseId) ||
          !config.GetUnitDatName(unitId).empty() || !config.GetUnitDatName(corpseId).empty() || config.GetUnitIdByName("TROJAN") >= 0)
        throw std::runtime_error("Trojan horse unit id collision");
      const auto* base = Rules::Unit(integer(data, "template_unit", 0, 32767));
      if (base == nullptr || base->type != Rules::UnitType::Creatable || base->cls != 13)
        throw std::runtime_error("Trojan horse template must be a siege unit");

      Settings candidate;
      candidate.m_woodCost = integer(data, "wood_cost", 0, 10000);
      candidate.m_goldCost = integer(data, "gold_cost", 0, 10000);
      candidate.m_population = integer(data, "population", 1, 20);
      candidate.m_workMs = integer(data, "work_ms", 16, 3600000);
      candidate.m_seats = integer(data, "seats", 1, 32);
      candidate.m_spearRange = integer(data, "spear_range", 1, 12);
      candidate.m_spearProjectile = integer(data, "spear_projectile", 0, 32767);
      const auto* spear = Rules::Unit(candidate.m_spearProjectile);
      if (spear == nullptr || spear->type != Rules::UnitType::Projectile)
        throw std::runtime_error("Trojan horse requires a native spear projectile");
      candidate.m_spearUnits = data.at("spear_units").get<std::vector<int>>();
      if (candidate.m_spearUnits.empty() || candidate.m_spearUnits.size() > 32)
        throw std::runtime_error("invalid Trojan horse spear unit list");
      for (const auto id : candidate.m_spearUnits)
      {
        const auto* passenger = Rules::Unit(id);
        if (passenger == nullptr || !canCarry(id, passenger->cls))
          throw std::runtime_error("invalid Trojan horse spear passenger");
      }
      candidate.m_build = label(data, "build");
      candidate.m_help = label(data, "help");
      candidate.m_populationWait = label(data, "population_wait");
      candidate.m_unloadHelp = label(data, "unload_help");
      replace(candidate.m_help, "{seats}", candidate.m_seats);
      replace(candidate.m_help, "{wood}", candidate.m_woodCost);
      replace(candidate.m_help, "{gold}", candidate.m_goldCost);
      replace(candidate.m_help, "{seconds}", (candidate.m_workMs + 999) / 1000);
      replace(candidate.m_help, "{population}", candidate.m_population);
      replace(candidate.m_populationWait, "{population}", candidate.m_population);

      const auto graphics = read(root / "graphics.json");
      const auto animations = read(root / "animations.json").at("graphics");
      HAL::DrsArchive archive;
      if (!HAL::DrsOpen((root / "trojan-horse.drs").string(), archive) || archive.entries.size() != 4 ||
          graphics.size() != 4 || animations.size() != 4)
        throw std::runtime_error("Trojan horse requires its four native graphics");
      std::array<ConfigManager::GraphicInfo, 4> art;
      constexpr std::array<int, 4> frames{1, 4, 1, 1};
      constexpr std::array<int, 4> flags{2, 3, 11, 2};
      constexpr int frameMs = 120;
      for (std::size_t i = 0; i < art.size(); ++i)
      {
        const int id = idleGraphic + static_cast<int>(i);
        const auto key = std::to_string(id);
        const auto& graphic = graphics.at(key);
        const auto& timing = animations.at(key);
        if (config.GetGraphic(id).Valid() || graphic.at("slp_id") != id || graphic.at("angle_count") != 8 ||
            graphic.at("frame_count") != frames[i] || graphic.at("layer") != 20 ||
            timing.at("frame_ms") != frameMs || timing.at("sequence") != flags[i] || timing.at("replay_ms") != 0)
          throw std::runtime_error("invalid Trojan horse art binding: " + key);
        const auto headers = HAL::SlpReadHeaders(archive, static_cast<uint32_t>(id));
        if (headers.size() != static_cast<std::size_t>(frames[i] * 5))
          throw std::runtime_error("invalid Trojan horse frame count: " + key);
        for (const auto& header : headers)
          if (header.width == 0 || header.width > 512 || header.height == 0 || header.height > 512)
            throw std::runtime_error("invalid Trojan horse frame bounds: " + key);
        art[i] = {id, id, frames[i], 8, 20};
      }

      auto unit = *base;
      unit.hp = integer(data, "hit_points", 1, 10000);
      unit.speed = Fixed32(integer(data, "speed_milli", 1, 10000)) / Fixed32(1000);
      unit.size_x = unit.size_y = Fixed32(integer(data, "radius_milli", 100, 490)) / Fixed32(1000);
      unit.los = Fixed32(integer(data, "line_of_sight", 1, 20));
      unit.search_radius = {};
      unit.dead_unit = corpseId;
      unit.standing_graphic = idleGraphic;
      unit.walking_graphic = walkGraphic;
      unit.attack_graphic = idleGraphic;
      unit.dying_graphic = dieGraphic;
      unit.attack_count = 0;
      unit.task_count = 0;
      unit.combat_level = 0;
      unit.garrison = candidate.m_seats;
      unit.garrison_type = 3;
      unit.garrison_heal = {};
      unit.armour_count = 2;
      unit.armours[0] = {4, 0};
      unit.armours[1] = {3, 0};
      unit.cost_count = 3;
      unit.costs[0] = {Rules::Res::Wood, static_cast<int16_t>(candidate.m_woodCost), true};
      unit.costs[1] = {Rules::Res::Gold, static_cast<int16_t>(candidate.m_goldCost), true};
      unit.costs[2] = {Rules::Res::PopulationHeadroom, static_cast<int16_t>(candidate.m_population), false};
      unit.storage_count = 3;
      unit.storage[0] = {Rules::Res::PopulationHeadroom, Fixed32(-candidate.m_population), Rules::StorageMode::WhileAlive};
      unit.storage[1] = {Rules::Res::CurrentPopulation, Fixed32(candidate.m_population), Rules::StorageMode::WhileAlive};
      unit.storage[2] = {Rules::Res::TotalUnitsOwned, Fixed32(1), Rules::StorageMode::Permanent};
      unit.train_time = (candidate.m_workMs + 999) / 1000;
      unit.train_loc = -1;
      unit.button = 0;
      unit.icon = -1;
      unit.sounds = {};
      unit.enabled = true;
      unit.damage_graphic_count = 0;
      Rules::UnitRules corpse;
      corpse.valid = true;
      corpse.type = Rules::UnitType::EyeCandy;
      corpse.standing_graphic = corpseGraphic;
      corpse.hide_in_editor = true;
      corpse.storage_count = 1;
      constexpr int decayResource = 12;
      corpse.storage[0] = {decayResource, Fixed32(integer(data, "corpse_seconds", 1, 3600)), 0};
      const Rules::UnitText name{"TROJAN", label(data, "name")};
      const Rules::UnitText remains{"TROJAN_CORPSE", name.label};
      // All external values and collisions are checked before registering the units.
      for (std::size_t i = 0; i < art.size(); ++i)
        config.RegisterGraphic(art[i], frameMs, flags[i]);
      if (!Rules::RegisterUnit(unitId, unit, name) || !Rules::RegisterUnit(corpseId, corpse, remains) ||
          !config.RegisterUnit(unitId, unit, name, candidate.m_help) ||
          !config.RegisterUnit(corpseId, corpse, remains, {}))
        throw std::runtime_error("Trojan horse registration invariant failed");
      loaded = std::move(candidate);
      return true;
    }
    catch (const std::exception& exception)
    {
      error = exception.what();
      return false;
    }
  }

  bool canCarry(int passengerId, int unitClass)
  {
    if (gorynych::isUnit(passengerId) || passengerId == treeman::unitId || passengerId == treeman::wildUnitId) return false;
    switch (unitClass)
    {
    case 4: case 0: case 6: case 44: case 45: case 46: case 50: case 56:
      return true;
    default:
      return false;
    }
  }

  const Settings& settings()
  {
    return loaded;
  }
}
