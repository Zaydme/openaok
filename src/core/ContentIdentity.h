#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Core::content
{
  constexpr uint32_t simulationRevision = 10;
  // Описывает совместимость симуляции и фактически загруженного пакета данных.
  struct Identity
  {
    uint32_t revision = simulationRevision;
    uint64_t dataHash = 0;
    bool operator==(const Identity&) const = default;
  };

  // FNV-1a detects accidental data differences; it is not an authentication hash.
  bool compute(const std::filesystem::path& root, const std::vector<std::string>& files,
    Identity& result, std::string& error);
  bool load(const std::filesystem::path& assets, std::string& error);
  const Identity& current();
}
