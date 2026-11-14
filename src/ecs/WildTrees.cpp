#include "ecs/WildTrees.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "core/Random.h"
#include "core/Treeman.h"
#include "core/Gorynych.h"
#include "ecs/Combat.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Score.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Grazer.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "pathfinding/Pathfinder.h"

namespace ECS::wild_trees
{
  namespace
  {
    namespace c = Components;
    namespace tree = Core::treeman;
    using Core::Fixed32;

    int64_t distanceSquared(int x, int y, int otherX, int otherY)
    {
      const int64_t dx = static_cast<int64_t>(x) - otherX;
      const int64_t dy = static_cast<int64_t>(y) - otherY;
      return dx * dx + dy * dy;
    }
  }

  namespace
  {
    void populateKind(Simulation& simulation, int wildId, int wildCount, int startDistanceTiles, uint64_t placementSalt)
    {
      auto& registry = simulation.GetRegistry();
      const auto& grid = simulation.GetGrid();
      const int width = grid.GetWidth(), height = grid.GetHeight();
      const auto layer = Navigation::LayerOfUnit(registry, grid, Core::PLAYER_GAIA, wildId);
      std::vector<uint8_t> occupied(static_cast<std::size_t>(width) * height, 0);
      const auto radius = Core::Rules::Unit(wildId)->size_x;
      for (const auto entity : registry.view<const c::Health, const c::Transform>())
      {
        const auto& at = registry.get<c::Transform>(entity).position;
        const auto clearance = radius + Navigation::RadiusOf(registry, entity);
        const int reach = clearance.ToInt() + 1;
        const int cx = Physics::TileOf(at.x), cy = Physics::TileOf(at.y);
        for (int y = std::max(0, cy - reach); y <= std::min(height - 1, cy + reach); ++y)
          for (int x = std::max(0, cx - reach); x <= std::min(width - 1, cx + reach); ++x)
          {
            const auto dx = at.x - Fixed32(x), dy = at.y - Fixed32(y);
            if (dx * dx + dy * dy <= clearance * clearance)
              occupied[static_cast<std::size_t>(y) * width + x] = 1;
          }
      }
      std::vector<std::pair<int, int>> candidates;
      candidates.reserve(occupied.size());
      constexpr int minimumRegionTiles = 16;
      const int64_t startDistance = static_cast<int64_t>(startDistanceTiles) * startDistanceTiles;
      for (int y = 1; y < height - 1; ++y)
        for (int x = 1; x < width - 1; ++x)
        {
          if (occupied[static_cast<std::size_t>(y) * width + x] || !grid.Passable(layer, x, y)) continue;
          const auto terrain = grid.GetTile(x, y).terrain;
          if (terrain == Core::TerrainType::ShallowWater || terrain == Core::TerrainType::Swamp) continue;
          if (std::any_of(simulation.player_starts.begin(), simulation.player_starts.end(), [&](const auto& start)
              { return distanceSquared(x, y, start.first, start.second) < startDistance; })) continue;
          const int region = Pathfinding::RegionAt(grid, x, y, layer);
          if (Pathfinding::RegionSize(grid, region, layer) < minimumRegionTiles) continue;
          candidates.emplace_back(x, y);
        }
      // A separate seeded stream leaves the map generator's random sequence intact.
      Core::Random random(static_cast<uint64_t>(simulation.Setup().seed) ^ placementSalt);
      for (std::size_t count = candidates.size(); count > 1; --count)
        std::swap(candidates[count - 1], candidates[random.NextInt(static_cast<uint32_t>(count))]);
      std::vector<int64_t> distances(candidates.size(), std::numeric_limits<int64_t>::max());
      std::vector<std::pair<int, int>> chosen;
      chosen.reserve(wildCount);
      for (int count = 0; count < wildCount && !candidates.empty(); ++count)
      {
        const auto best = std::max_element(distances.begin(), distances.end());
        if (*best < 0) break;
        const auto index = static_cast<std::size_t>(best - distances.begin());
        const auto at = candidates[index];
        chosen.push_back(at);
        for (std::size_t i = 0; i < candidates.size(); ++i)
          distances[i] = std::min(distances[i], distanceSquared(at.first, at.second,
            candidates[i].first, candidates[i].second));
        distances[index] = -1;
      }
      if (chosen.size() != static_cast<std::size_t>(wildCount))
        std::cerr << "[WildTrees] Not enough open land: wild_count=" << wildCount
                  << ", placed=" << chosen.size() << '\n';
      for (const auto& [x, y] : chosen)
      {
        const auto entity = Units::Spawn(registry, wildId, Core::PLAYER_GAIA, {Fixed32(x), Fixed32(y)});
        if (entity == entt::null)
          std::cerr << "[WildTrees] Cannot spawn registered wild_unit_id=" << wildId << '\n';
      }
    }

  }

  void populate(Simulation& simulation)
  {
    const auto& trees = tree::settings();
    const auto& dragons = Core::gorynych::settings();
    populateKind(simulation, tree::wildUnitId, trees.m_wildCount, trees.m_wildStartDistance, 0x57414C4B54524545ull);
    populateKind(simulation, Core::gorynych::wildUnitId, dragons.m_wildCount, dragons.m_wildStartDistance, 0x474F52594E594348ull);
  }

  void adopt(entt::registry& registry)
  {
    static std::vector<entt::entity> claimed = []
    {
      std::vector<entt::entity> entities;
      entities.reserve(tree::settings().m_wildCount);
      return entities;
    }();
    claimed.clear();
    for (const auto entity : registry.view<const c::Grazer, const c::Identity, const c::Owner>())
      if ((registry.get<c::Identity>(entity).unit_id == tree::wildUnitId ||
           registry.get<c::Identity>(entity).unit_id == Core::gorynych::wildUnitId) &&
          registry.get<c::Owner>(entity).player_id != Core::PLAYER_GAIA && Combat::Alive(registry, entity))
        claimed.push_back(entity);
    std::sort(claimed.begin(), claimed.end());
    for (const auto entity : claimed)
    {
      const auto player = registry.get<c::Owner>(entity).player_id;
      const int unitId = registry.get<c::Identity>(entity).unit_id == tree::wildUnitId ? tree::unitId : Core::gorynych::unitId;
      Units::Become(registry, entity, unitId);
      registry.remove<c::Grazer>(entity);
      registry.emplace_or_replace<c::Combat>(entity);
      Combat::FirstLook(registry, entity);
      Players::Contribute(registry, player, unitId, Core::Rules::StorageMode::Permanent, +1);
      Score::UnitCreated(registry, player, unitId);
    }
  }
}
