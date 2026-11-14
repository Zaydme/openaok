#include "core/ContentIdentity.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace Core::content
{
  namespace
  {
    Identity loaded;
    constexpr uint64_t offset = 14695981039346656037ull;
    constexpr uint64_t prime = 1099511628211ull;
  }

  bool compute(const std::filesystem::path& root, const std::vector<std::string>& files,
    Identity& result, std::string& error)
  {
    error.clear();
    if (files.empty())
    {
      error = "the content file list is empty";
      return false;
    }
    auto ordered = files;
    std::sort(ordered.begin(), ordered.end());
    if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end())
    {
      error = "duplicate content file";
      return false;
    }
    uint64_t hash = offset;
    const auto mix = [&hash](uint8_t byte)
    {
      hash = (hash ^ byte) * prime;
    };
    std::array<char, 65536> buffer{};
    for (const auto& name : ordered)
    {
      std::ifstream input(root / name, std::ios::binary);
      if (!input)
      {
        error = "cannot read content file: " + name;
        return false;
      }
      for (const auto byte : name) mix(static_cast<uint8_t>(byte));
      mix(0);
      uint64_t size = 0;
      while (input)
      {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        size += static_cast<uint64_t>(count);
        for (std::streamsize i = 0; i < count; ++i) mix(static_cast<uint8_t>(buffer[i]));
      }
      if (!input.eof())
      {
        error = "cannot finish reading content file: " + name;
        return false;
      }
      for (int shift = 0; shift < 64; shift += 8) mix(static_cast<uint8_t>(size >> shift));
    }
    result = Identity{simulationRevision, hash};
    return true;
  }

  bool load(const std::filesystem::path& assets, std::string& error)
  {
    // Include rules, localisation, art and the archive holding RMS and AI scripts.
    const std::vector<std::string> files = {
      "extracted_data/ai.json", "extracted_data/animations.json",
      "extracted_data/annexes.dat.json", "extracted_data/annexes.overrides.json",
      "extracted_data/buildings.json", "extracted_data/graphics.json",
      "extracted_data/hotkeys.json", "extracted_data/interface.json",
      "extracted_data/media.json", "extracted_data/rules.json",
      "extracted_data/shadows.json", "extracted_data/strings.json",
      "extracted_data/techtree.json", "extracted_data/terrain.json",
      "extracted_data/units.json", "original/Data/gamedata.drs",
      "original/Data/graphics.drs", "original/Data/interfac.drs",
      "original/Data/terrain.drs", "original/Data/sounds.drs",
      "../mods/worldcraft/manifest.json", "../mods/worldcraft/rules.json",
      "../mods/worldcraft/graphics.json", "../mods/worldcraft/strings.ru.json",
      "../mods/worldcraft/treeman/unit.json", "../mods/worldcraft/treeman/graphics.json",
      "../mods/worldcraft/transport/unit.json", "../mods/worldcraft/transport/graphics.json",
      "../mods/worldcraft/transport/animations.json", "../mods/worldcraft/transport/trojan-horse.drs",
      "../mods/worldcraft/gorynych/unit.json", "../mods/worldcraft/gorynych/graphics.json",
      "../mods/worldcraft/gorynych/animations.json", "../mods/worldcraft/gorynych/gorynych.drs",
      "../mods/worldcraft/treeman/animations.json", "../mods/worldcraft/treeman/walking-tree.drs"
    };
    Identity candidate;
    if (!compute(assets, files, candidate, error)) return false;
    loaded = candidate;
    return true;
  }

  const Identity& current()
  {
    return loaded;
  }
}
