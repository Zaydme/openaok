#pragma once

#include <filesystem>
#include <string>

namespace Core::gorynych
{
  inline constexpr int unitId = 1005;
  inline constexpr int corpseId = 1006;
  inline constexpr int wildUnitId = 1007;
  inline constexpr int projectileId = 1008;
  inline constexpr int flightLift = 64;
  inline constexpr int heads = 3;
  inline constexpr int idleGraphic = 59020;
  inline constexpr int walkGraphic = 59021;
  inline constexpr int attackGraphic = 59022;
  inline constexpr int dieGraphic = 59023;
  inline constexpr int corpseGraphic = 59024;
  inline constexpr const char* archivePath = "../mods/worldcraft/gorynych/gorynych.drs";

  // Хранит проверенные параметры выращивания, дикой популяции, приручения и подписи Горыныча.
  struct Settings
  {
    int m_woodCost = 0;
    int m_goldCost = 0;
    int m_population = 0;
    int m_workMs = 0;
    int m_wildCount = 0;
    int m_wildStartDistance = 0;
    int m_tameRadius = 0;
    std::string m_grow;
    std::string m_help;
    std::string m_populationWait;
    std::string m_claimedNotice;
  };

  // Called once after the base rules and art tables, before any player is made.
  bool load(const std::filesystem::path& root, std::string& error);
  const Settings& settings();
  constexpr bool isUnit(int id)
  {
    return id == unitId || id == wildUnitId;
  }
}
