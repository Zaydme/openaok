#include "core/Gorynych.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "core/ConfigManager.h"
#include "core/Rules.h"
#include "hal/SlpFile.h"

namespace Core::gorynych
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
      if (data.at("schema") != 1 || data.at("unit_id") != unitId || data.at("corpse_id") != corpseId ||
          data.at("wild_unit_id") != wildUnitId || data.at("projectile_id") != projectileId)
        throw std::runtime_error("unsupported Gorynych unit schema or ids");
      auto& config = ConfigManager::Get();
      if (Rules::Unit(projectileId) || !config.GetUnitDatName(projectileId).empty() || Rules::Unit(unitId) || Rules::Unit(corpseId) || Rules::Unit(wildUnitId) ||
          !config.GetUnitDatName(unitId).empty() || !config.GetUnitDatName(corpseId).empty() ||
          !config.GetUnitDatName(wildUnitId).empty() || config.GetUnitIdByName("GORYNYCH") >= 0 ||
          config.GetUnitIdByName("GORYNYCH_WILD") >= 0)
        throw std::runtime_error("Gorynych unit id collision");
      const auto* base = Rules::Unit(integer(data, "template_unit", 0, 32767));
      if (base == nullptr || base->type != Rules::UnitType::Creatable || base->cls != 0)
        throw std::runtime_error("Gorynych template must be an archer unit");

      const auto* fire = Rules::Unit(integer(data, "projectile_template", 0, 32767));
      if (fire == nullptr || fire->type != Rules::UnitType::Projectile)
        throw std::runtime_error("Gorynych requires a native fire projectile");

      Settings candidate;
      candidate.m_woodCost = integer(data, "wood_cost", 0, 10000);
      candidate.m_goldCost = integer(data, "gold_cost", 0, 10000);
      candidate.m_population = integer(data, "population", 1, 20);
      candidate.m_workMs = integer(data, "work_ms", 16, 3600000);
      candidate.m_wildCount = integer(data, "wild_count", 1, 64);
      candidate.m_wildStartDistance = integer(data, "wild_start_distance", 4, 32);
      candidate.m_tameRadius = integer(data, "tame_radius", 1, 8);
      candidate.m_grow = label(data, "grow");
      candidate.m_help = label(data, "help");
      candidate.m_populationWait = label(data, "population_wait");
      candidate.m_claimedNotice = label(data, "claimed_notice");
      replace(candidate.m_help, "{wood}", candidate.m_woodCost);
      replace(candidate.m_help, "{gold}", candidate.m_goldCost);
      replace(candidate.m_help, "{seconds}", (candidate.m_workMs + 999) / 1000);
      replace(candidate.m_help, "{population}", candidate.m_population);
      replace(candidate.m_populationWait, "{population}", candidate.m_population);

      const auto graphics = read(root / "graphics.json");
      const auto animations = read(root / "animations.json").at("graphics");
      HAL::DrsArchive archive;
      if (!HAL::DrsOpen((root / "gorynych.drs").string(), archive) || archive.entries.size() != 5 ||
          graphics.size() != 5 || animations.size() != 5)
        throw std::runtime_error("Gorynych requires its five native graphics");
      std::array<ConfigManager::GraphicInfo, 5> art;
      constexpr std::array<int, 5> frames{4, 4, 4, 4, 1};
      constexpr std::array<int, 5> flags{3, 3, 3, 11, 2};
      constexpr int frameMs = 160;
      for (std::size_t i = 0; i < art.size(); ++i)
      {
        const int id = idleGraphic + static_cast<int>(i);
        const auto key = std::to_string(id);
        const auto& graphic = graphics.at(key);
        const auto& timing = animations.at(key);
        if (config.GetGraphic(id).Valid() || graphic.at("slp_id") != id || graphic.at("angle_count") != 8 ||
            graphic.at("frame_count") != frames[i] || graphic.at("layer") != 20 ||
            timing.at("frame_ms") != frameMs || timing.at("sequence") != flags[i] || timing.at("replay_ms") != 0)
          throw std::runtime_error("invalid Gorynych art binding: " + key);
        const auto headers = HAL::SlpReadHeaders(archive, static_cast<uint32_t>(id));
        if (headers.size() != static_cast<std::size_t>(frames[i] * 5))
          throw std::runtime_error("invalid Gorynych frame count: " + key);
        for (const auto& header : headers)
          if (header.width == 0 || header.width > 512 || header.height == 0 || header.height > 512)
            throw std::runtime_error("invalid Gorynych frame bounds: " + key);
        art[i] = {id, id, frames[i], 8, 20};
      }

      auto unit = *base;
      unit.hp = integer(data, "hit_points", 1, 10000);
      unit.speed = Fixed32(integer(data, "speed_milli", 1, 10000)) / Fixed32(1000);
      unit.size_x = unit.size_y = Fixed32(integer(data, "radius_milli", 100, 490)) / Fixed32(1000);
      unit.los = Fixed32(integer(data, "line_of_sight", 1, 20));
      unit.search_radius = unit.los;
      unit.dead_unit = corpseId;
      unit.standing_graphic = idleGraphic;
      unit.walking_graphic = walkGraphic;
      unit.attack_graphic = attackGraphic;
      unit.dying_graphic = dieGraphic;
      unit.frame_delay = integer(data, "attack_frame", 1, frames[2] - 1);
      unit.reload = Fixed32(integer(data, "reload_ms", frames[2] * frameMs, 30000)) / Fixed32(1000);
      unit.attack_count = 1;
      unit.attacks[0] = {3, static_cast<int16_t>(integer(data, "fire_attack", 1, 1000))};
      unit.projectile = projectileId;
      unit.secondary_projectile = -1;
      unit.max_range = Fixed32(integer(data, "range", 1, 20));
      unit.min_range = {};
      unit.blast_width = Fixed32(integer(data, "splash_milli", 1, 2000)) / Fixed32(1000);
      unit.blast_level = 2;
      unit.accuracy = 100;
      unit.dispersion = {};
      unit.task_count = 1;
      unit.tasks[0] = {};
      unit.tasks[0].action = Rules::TaskAction::Combat;
      unit.garrison = 0;
      unit.armour_count = 2;
      unit.armours[0] = {4, static_cast<int16_t>(integer(data, "melee_armour", 0, 100))};
      unit.armours[1] = {3, static_cast<int16_t>(integer(data, "pierce_armour", 0, 100))};
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
      const Rules::UnitText name{"GORYNYCH", label(data, "name")};
      const Rules::UnitText remains{"GORYNYCH_CORPSE", name.label};
      auto wild = unit;
      constexpr int livestockClass = 58;
      wild.cls = livestockClass;
      wild.enabled = false;
      wild.los = Fixed32(candidate.m_tameRadius);
      wild.attack_count = 0;
      wild.combat_level = 0;
      wild.search_radius = {};
      wild.cost_count = 0;
      wild.storage_count = 0;
      wild.train_time = 0;
      wild.task_count = 2;
      wild.tasks[0] = {};
      wild.tasks[0].action = Rules::TaskAction::Graze;
      wild.tasks[1] = {};
      wild.tasks[1].action = Rules::TaskAction::AutoConvert;
      const Rules::UnitText wildName{"GORYNYCH_WILD", label(data, "wild_name")};
      auto wildHelp = label(data, "wild_help");
      replace(wildHelp, "{population}", candidate.m_population);
      replace(wildHelp, "{radius}", candidate.m_tameRadius);
      auto projectile = *fire;
      projectile.speed = Fixed32(7);
      projectile.dying_graphic = -1;
      const Rules::UnitText fireName{"GORYNYCH_FIRE", name.label};
      // All external values and collisions are checked before registering the units.
      for (std::size_t i = 0; i < art.size(); ++i)
        config.RegisterGraphic(art[i], frameMs, flags[i]);
      if (!Rules::RegisterUnit(unitId, unit, name) || !Rules::RegisterUnit(corpseId, corpse, remains) ||
          !Rules::RegisterUnit(wildUnitId, wild, wildName) ||
          !Rules::RegisterUnit(projectileId, projectile, fireName) ||
          !config.RegisterUnit(projectileId, projectile, fireName, {}) ||
          !config.RegisterUnit(unitId, unit, name, candidate.m_help) ||
          !config.RegisterUnit(corpseId, corpse, remains, {}) ||
          !config.RegisterUnit(wildUnitId, wild, wildName, wildHelp))
        throw std::runtime_error("Gorynych registration invariant failed");
      loaded = std::move(candidate);
      return true;
    }
    catch (const std::exception& exception)
    {
      error = exception.what();
      return false;
    }
  }

  const Settings& settings()
  {
    return loaded;
  }
}
