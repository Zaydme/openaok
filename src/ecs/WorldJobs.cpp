#include "ecs/WorldJobs.h"

#include <algorithm>
#include <vector>
#include "core/ConfigManager.h"
#include "core/Worldcraft.h"
#include "core/Treeman.h"
#include "core/TrojanHorse.h"
#include "ecs/Population.h"
#include "ecs/Units.h"
#include "ecs/Buildings.h"
#include "ecs/Earthworks.h"
#include "ecs/Fog.h"
#include "ecs/Navigation.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Flight.h"
#include "ecs/Vision.h"
#include "ecs/Walls.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Path.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Worker.h"
#include "ecs/components/WorldJob.h"

namespace ECS::world_jobs
{
  namespace
  {
    namespace c = Components;
    using Core::Fixed32;

    bool isWorker(const entt::registry& registry, entt::entity entity, Core::PlayerId player)
    {
      if (player == Core::PLAYER_GAIA || !registry.valid(entity) ||
          !registry.all_of<c::Worker, c::Owner, c::Transform, c::Health>(entity) ||
          registry.any_of<c::Dying, c::Corpse>(entity)) return false;
      return registry.get<c::Worker>(entity).gatherer < 0 &&
        registry.get<c::Owner>(entity).player_id == player &&
        registry.get<c::Health>(entity).current_hp > 0;
    }

    bool isWork(const entt::registry& registry, entt::entity site, Core::PlayerId player)
    {
      return registry.valid(site) && registry.all_of<c::WorldJob, c::Owner>(site) &&
        registry.get<c::Owner>(site).player_id == player &&
        registry.get<c::WorldJob>(site).phase == c::WorldJobPhase::Working;
    }

    void showArt(entt::registry& registry, entt::entity entity, int unit)
    {
      const auto& config = Core::ConfigManager::Get();
      const auto art = config.GetUnitGraphicById(unit, "idle_graphic0");
      const bool isCustomUnit = unit == Core::treeman::unitId || unit == Core::trojan_horse::unitId || Core::gorynych::isUnit(unit);
      c::Sprite sprite;
      sprite.slp_id = art.slp_id;
      sprite.still = true;
      sprite.angles = isCustomUnit ? art.angle_count : 1;
      sprite.frames_per_angle = isCustomUnit ? art.frame_count : 1;
      sprite.is_file_sprite = isCustomUnit;
      registry.emplace_or_replace<c::Sprite>(entity, sprite);
      registry.emplace_or_replace<c::Identity>(entity, unit);
    }

    void selectNext(entt::registry& registry, entt::entity worker, const c::WorldJobQueue& queue)
    {
      auto& state = registry.get<c::Worker>(worker);
      const auto next = queue.count != 0 ? queue.sites[0] : entt::null;
      if (state.target_building != next)
      {
        registry.remove<c::Path>(worker);
        state.working = false;
        state.build_attempts = 0;
      }
      state.target_building = next;
      state.state = queue.count != 0 ? Core::UnitState::WorldJob : Core::UnitState::Idle;
      state.target_resource = entt::null;
    }
  }

  bool canPlant(const entt::registry& registry, const Physics::Grid& grid,
    Core::PlayerId player, int x, int y)
  {
    if (player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers || !grid.InBounds(x, y) || !grid.IsWalkable(x, y) ||
        !Vision::Visible(registry, player, x, y)) return false;
    const auto terrain = grid.GetTile(x, y).terrain;
    if (terrain == Core::TerrainType::ShallowWater || terrain == Core::TerrainType::Swamp ||
        terrain == Core::TerrainType::Ice || terrain == Core::TerrainType::Farm ||
        terrain == Core::TerrainType::FarmBuild1 || terrain == Core::TerrainType::FarmBuild2 ||
        terrain == Core::TerrainType::FarmBuild3) return false;
    const c::Vector2D at{Fixed32(x), Fixed32(y)};
    const auto ground = Buildings::TileBounds(at);
    for (const auto entity : registry.view<const c::WorldJob>())
    {
      const auto& job = registry.get<c::WorldJob>(entity);
      if (job.tileX == x && job.tileY == y) return false;
      if (Core::isEarthwork(job.kind) && earthworks::bounds(job.tileX, job.tileY, job.kind, job.rise).DistanceFrom(at).value == 0) return false;
    }
    // Pending foundations and the full body of a unit also reserve ground.
    for (const auto entity : registry.view<const c::Transform>())
    {
      const auto& position = registry.get<c::Transform>(entity).position;
      if (registry.all_of<c::Building, c::Identity>(entity))
      {
        const auto other = Buildings::FootprintBounds(position, registry.get<c::Identity>(entity).unit_id);
        if (ground.min_x < other.max_x && ground.max_x > other.min_x &&
            ground.min_y < other.max_y && ground.max_y > other.min_y) return false;
      }
      else if (!flight::isFlying(registry, entity) && registry.all_of<c::Health>(entity) && !registry.any_of<c::Dying, c::Corpse>(entity) &&
          ground.DistanceFrom(position) <= Navigation::RadiusOf(registry, entity)) return false;
    }
    return true;
  }

  bool order(entt::registry& registry, Physics::Grid& grid, const Core::WorldJobCommand& command, int levels)
  {
    if (!Core::isMountainHeight(command.height) || command.m_flowDirection > 8 ||
        (command.kind != Core::WorldJobKind::DigRiver && command.m_flowDirection != 0)) return false;
    if (levels == 0) levels = command.kind == Core::WorldJobKind::RaiseGround ? command.height : 1;
    if (command.player == Core::PLAYER_GAIA || command.player >= Core::kMaxPlayers ||
        !Core::isWorldJobKind(command.kind) || !grid.InBounds(command.tileX, command.tileY) ||
        !Vision::Visible(registry, command.player, command.tileX, command.tileY)) return false;
    auto crew = command.workers;
    std::sort(crew.begin(), crew.end());
    crew.erase(std::unique(crew.begin(), crew.end()), crew.end());
    std::erase_if(crew, [&](entt::entity worker)
    {
      if (!isWorker(registry, worker, command.player)) return true;
      const auto* queue = registry.try_get<c::WorldJobQueue>(worker);
      return command.append && queue != nullptr && queue->count >= c::worldJobQueueLimit;
    });
    if (crew.empty()) return false;
    entt::entity site = entt::null;
    int active = 0;
    for (const auto candidate : registry.view<const c::WorldJob, const c::Owner>())
    {
      if (registry.get<c::Owner>(candidate).player_id != command.player) continue;
      ++active;
      const auto& job = registry.get<c::WorldJob>(candidate);
      if (job.kind == command.kind && job.tileX == command.tileX && job.tileY == command.tileY &&
          job.phase == c::WorldJobPhase::Working) site = candidate;
    }
    if (site == entt::null)
    {
      const auto& rules = Core::worldcraft::plantRules();
      const bool isEarthwork = Core::isEarthwork(command.kind);
      if (isEarthwork ? !earthworks::canWork(registry, grid, command.player, command.tileX, command.tileY, command.kind, levels) :
          !canPlant(registry, grid, command.player, command.tileX, command.tileY)) return false;
      const auto& tile = grid.GetTile(command.tileX, command.tileY);
      const int targetLevel = isEarthwork ? earthworks::targetLevel(tile, command.kind, levels) : 0;
      const int steps = isEarthwork ? std::max(1, std::abs(targetLevel - tile.elevation)) : 1;
      const auto work = Core::worldcraft::workRules(command.kind);
      const int cost = work.woodCost * steps;
      auto* player = Players::Find(registry, command.player);
      if (active >= activeSiteLimit || player == nullptr ||
          Players::Stock(*player, Core::ResourceType::Wood) < cost ||
          Players::Stock(*player, Core::ResourceType::Gold) < work.goldCost) return false;
      site = registry.create();
      const c::Vector2D at{Fixed32(command.tileX), Fixed32(command.tileY)};
      registry.emplace<c::Transform>(site, at, at);
      registry.emplace<c::Owner>(site, command.player);
      registry.emplace<c::Selectable>(site);
      registry.emplace<c::Planted>(site);
      registry.emplace<c::WorldJob>(site, c::WorldJob{command.kind, c::WorldJobPhase::Working,
        command.tileX, command.tileY, cost, work.workMs * steps, {}, isEarthwork ? 0 : rules.growthMs, false, targetLevel,
        isEarthwork && command.kind == Core::WorldJobKind::RaiseGround ? steps : 1});
      registry.get<c::WorldJob>(site).paidGold = work.goldCost;
      registry.get<c::WorldJob>(site).m_flowDirection = command.kind == Core::WorldJobKind::DigRiver ?
        std::max<uint8_t>(1, command.m_flowDirection) : 0;
      if (isEarthwork)
      {
        c::Sprite marker;
        marker.slp_id = Core::worldcraft::workMarkerSlp(command.kind);
        marker.still = true;
        marker.angles = 1;
        registry.emplace<c::Sprite>(site, marker);
        registry.emplace<c::Identity>(site, rules.siteUnit);
      }
      else showArt(registry, site, Core::isUnitConstruction(command.kind) ? Core::worldcraft::builtUnit(command.kind) : rules.siteUnit);
      Players::AddStock(*player, Core::ResourceType::Wood, -cost);
      Players::AddStock(*player, Core::ResourceType::Gold, -work.goldCost);
    }
    for (const auto worker : crew)
    {
      if (!command.append || !registry.all_of<c::WorldJobQueue>(worker)) Orders::clearActivity(registry, worker);
      auto& queue = registry.get_or_emplace<c::WorldJobQueue>(worker);
      const auto end = queue.sites.begin() + queue.count;
      if (std::find(queue.sites.begin(), end, site) == end) queue.sites[queue.count++] = site;
      selectNext(registry, worker, queue);
    }
    return true;
  }

  std::vector<std::pair<int, int>> layout(const Physics::Grid& grid, int fromX, int fromY,
    int toX, int toY, Core::PlantShape shape)
  {
    if (!grid.InBounds(fromX, fromY) || !grid.InBounds(toX, toY)) return {};
    const int width = std::abs(toX - fromX) + 1;
    const int height = std::abs(toY - fromY) + 1;
    if (shape == Core::PlantShape::Line)
    {
      if (std::max(width, height) > activeSiteLimit) return {};
      return Walls::Line(fromX, fromY, toX, toY);
    }
    if (shape != Core::PlantShape::Area || static_cast<int64_t>(width) * height > activeSiteLimit) return {};
    std::vector<std::pair<int, int>> tiles;
    tiles.reserve(static_cast<std::size_t>(width) * height);
    const int minX = std::min(fromX, toX), maxX = std::max(fromX, toX);
    const int minY = std::min(fromY, toY), maxY = std::max(fromY, toY);
    for (int y = minY; y <= maxY; ++y)
      for (int x = minX; x <= maxX; ++x) tiles.emplace_back(x, y);
    // Fill from the inside out, keeping the unplanted outer tiles open for the crew.
    std::sort(tiles.begin(), tiles.end(), [&](const auto& a, const auto& b)
    {
      const int depthA = std::min({a.first - minX, maxX - a.first, a.second - minY, maxY - a.second});
      const int depthB = std::min({b.first - minX, maxX - b.first, b.second - minY, maxY - b.second});
      return depthA != depthB ? depthA > depthB : a < b;
    });
    return tiles;
  }

  std::vector<std::pair<int, int>> layout(const Physics::Grid& grid, const Core::PlantLayoutCommand& command)
  {
    if (!Core::isWorldJobKind(command.kind) || !Core::isMountainHeight(command.height) ||
        !Core::isWaterWidth(command.m_width) || (!Core::isWaterwork(command.kind) && command.m_width != 1)) return {};
    const bool isReversed = Core::isWaterwork(command.kind) && command.shape == Core::PlantShape::Line &&
      std::pair{command.fromX, command.fromY} > std::pair{command.toX, command.toY};
    auto centers = isReversed ? layout(grid, command.toX, command.toY, command.fromX, command.fromY, command.shape) :
      layout(grid, command.fromX, command.fromY, command.toX, command.toY, command.shape);
    if (isReversed) std::reverse(centers.begin(), centers.end());
    if (!Core::isWaterwork(command.kind) || command.shape == Core::PlantShape::Area) return centers;
    std::vector<std::pair<int, int>> tiles;
    tiles.reserve(activeSiteLimit);
    const auto add = [&](int x, int y)
    {
      const std::pair<int, int> tile{x, y};
      if (!grid.InBounds(x, y) || std::find(tiles.begin(), tiles.end(), tile) != tiles.end()) return true;
      if (tiles.size() >= activeSiteLimit) return false;
      tiles.push_back(tile);
      return true;
    };
    // Excavate the centre before the banks; connect diagonal cells along an edge.
    for (int ring = 0; ring <= command.m_width / 2; ++ring)
    {
      for (std::size_t index = 0; index < centers.size(); ++index)
      {
        const auto [x, y] = centers[index];
        for (int dy = -ring; dy <= ring; ++dy)
          for (int dx = -ring; dx <= ring; ++dx)
          {
            if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
            if (!add(x + dx, y + dy)) return {};
            if (index == 0) continue;
            const auto [previousX, previousY] = centers[index - 1];
            if (x != previousX && y != previousY)
            {
              const int bridgeX = std::max(x, previousX);
              const int bridgeY = x > previousX ? previousY : y;
              if (!add(bridgeX + dx, bridgeY + dy)) return {};
            }
          }
      }
    }
    return tiles;
  }

  void plantLayout(entt::registry& registry, Physics::Grid& grid, const Core::PlantLayoutCommand& command)
  {
    const auto tiles = layout(grid, command);
    const uint8_t flowDirection = command.kind == Core::WorldJobKind::DigRiver ?
      Core::riverDirection(command.fromX, command.fromY, command.toX, command.toY) : 0;
    bool append = command.append;
    for (const auto& [x, y] : tiles)
    {
      if (order(registry, grid, {command.workers, command.kind, x, y, append, command.player, command.height, flowDirection}, layoutLevels(command, x, y)))
        append = true;
    }
  }

  int layoutLevels(const Core::PlantLayoutCommand& command, int, int)
  {
    return command.kind == Core::WorldJobKind::RaiseGround ? command.height : 1;
  }

  void cancel(entt::registry& registry, Physics::Grid& grid, entt::entity site, Core::PlayerId player)
  {
    if (!registry.valid(site) || !registry.all_of<c::WorldJob, c::Owner>(site) ||
        registry.get<c::Owner>(site).player_id != player) return;
    const auto job = registry.get<c::WorldJob>(site);
    if (job.phase == c::WorldJobPhase::Working)
    {
      const int refund = (Fixed32(job.paidWood) *
        (Fixed32(1) - job.workedMs / Fixed32(job.workMs))).ToInt();
      if (auto* seat = Players::Find(registry, player))
      {
        Players::AddStock(*seat, Core::ResourceType::Wood, refund);
        const int gold = (Fixed32(job.paidGold) *
          (Fixed32(1) - job.workedMs / Fixed32(job.workMs))).ToInt();
        Players::AddStock(*seat, Core::ResourceType::Gold, gold);
      }
    }
    Fog::Going(registry, site);
    if (job.hasBlocked) grid.SetWalkable(job.tileX, job.tileY, true);
    registry.destroy(site);
  }

  void prepare(entt::registry& registry)
  {
    // EnTT allows removing the current entity's component during this pass.
    for (const auto worker : registry.view<c::WorldJobQueue>())
    {
      const auto* owner = registry.try_get<c::Owner>(worker);
      if (owner == nullptr || !isWorker(registry, worker, owner->player_id) ||
          registry.get<c::Worker>(worker).state != Core::UnitState::WorldJob)
      {
        registry.remove<c::WorldJobQueue>(worker);
        continue;
      }
      auto& queue = registry.get<c::WorldJobQueue>(worker);
      uint16_t kept = 0;
      for (uint16_t i = 0; i < queue.count; ++i)
        if (isWork(registry, queue.sites[i], owner->player_id)) queue.sites[kept++] = queue.sites[i];
      queue.count = kept;
      selectNext(registry, worker, queue);
      if (kept == 0) registry.remove<c::WorldJobQueue>(worker);
    }
  }

  void update(entt::registry& registry, Physics::Grid& grid, int32_t dtMs)
  {
    if (dtMs <= 0) return;
    // Site order defines completion and refund order, independent of storage layout.
    static std::vector<entt::entity> sites;
    sites.clear();
    for (const auto site : registry.view<c::WorldJob>()) sites.push_back(site);
    std::sort(sites.begin(), sites.end());
    constexpr int terrainChangesPerTick = 2;
    int terrainChanges = 0;
    for (const auto site : sites)
    {
      auto& job = registry.get<c::WorldJob>(site);
      const auto player = registry.get<c::Owner>(site).player_id;
      const auto& rules = Core::worldcraft::plantRules();
      if (job.phase == c::WorldJobPhase::Growing)
      {
        job.growthLeftMs = std::max(0, job.growthLeftMs - dtMs);
        if (job.growthLeftMs != 0) continue;
        Fog::Going(registry, site);
        showArt(registry, site, rules.treeUnit);
        const auto* tree = Core::Rules::Unit(rules.treeUnit);
        int wood = 0;
        for (int i = 0; i < tree->storage_count; ++i)
          if (tree->storage[i].resource == Core::Rules::Res::Wood &&
              tree->storage[i].mode == 0) wood += tree->storage[i].amount.ToInt();
        registry.emplace<c::Resource>(site, Core::ResourceType::Wood, wood);
        registry.get<c::Owner>(site).player_id = Core::PLAYER_GAIA;
        grid.GetTileMut(job.tileX, job.tileY).has_tree = true;
        registry.remove<c::WorldJob>(site);
        continue;
      }
      bool hasAssigned = false;
      int working = 0;
      for (const auto worker : registry.view<const c::WorldJobQueue>())
      {
        if (!isWorker(registry, worker, player)) continue;
        const auto& queue = registry.get<c::WorldJobQueue>(worker);
        const auto end = queue.sites.begin() + queue.count;
        hasAssigned = hasAssigned || std::find(queue.sites.begin(), end, site) != end;
        const auto& state = registry.get<c::Worker>(worker);
        if (queue.count != 0 && queue.sites[0] == site && state.state == Core::UnitState::WorldJob &&
            state.target_building == site && state.working) ++working;
      }
      if (!hasAssigned)
      {
        cancel(registry, grid, site, player);
        continue;
      }
      if (working == 0) continue;
      if (!Core::isEarthwork(job.kind) && !job.hasBlocked)
      {
        const auto ground = Buildings::TileBounds({Fixed32(job.tileX), Fixed32(job.tileY)});
        bool isOccupied = false;
        for (const auto unit : registry.view<const c::Transform, const c::Health>())
        {
          // Buildings reserve their rectangular footprint in the grid. Their
          // unit radius can cover unrelated free tiles beside the building.
          if (registry.any_of<c::Dying, c::Corpse, c::Building>(unit) || flight::isFlying(registry, unit)) continue;
          if (ground.DistanceFrom(registry.get<c::Transform>(unit).position) < Navigation::RadiusOf(registry, unit))
            isOccupied = true;
        }
        if (isOccupied) continue;
        if (!grid.IsWalkable(job.tileX, job.tileY))
        {
          cancel(registry, grid, site, player);
          continue;
        }
        grid.SetWalkable(job.tileX, job.tileY, false);
        job.hasBlocked = true;
      }
      job.workedMs = std::min(Fixed32(job.workMs), job.workedMs +
        Fixed32(dtMs) * Fixed32(working + 2) / Fixed32(3));
      if (job.workedMs < Fixed32(job.workMs)) continue;
      if (Core::isEarthwork(job.kind))
      {
        job.clearanceWaitMs = std::max(0, job.clearanceWaitMs - dtMs);
        if (terrainChanges >= terrainChangesPerTick || job.clearanceWaitMs > 0) continue;
        ++terrainChanges;
        if (!earthworks::finish(registry, grid, site))
        {
          job.clearanceWaitMs = 1000;
          continue;
        }
        Fog::Going(registry, site);
        registry.destroy(site);
        continue;
      }
      if (Core::isUnitConstruction(job.kind))
      {
        if (!Population::HasRoom(registry, player, Core::worldcraft::builtPopulation(job.kind))) continue;
        const c::Vector2D at{Fixed32(job.tileX), Fixed32(job.tileY)};
        const auto unit = Units::Spawn(registry, Core::worldcraft::builtUnit(job.kind), player, at);
        if (unit == entt::null) continue;
        grid.SetWalkable(job.tileX, job.tileY, true);
        Fog::Going(registry, site);
        registry.destroy(site);
        continue;
      }
      Fog::Going(registry, site);
      job.phase = c::WorldJobPhase::Growing;
      showArt(registry, site, rules.growingUnit);
    }
    prepare(registry);
  }

  void observe(entt::registry& registry)
  {
    const auto* map = Vision::MapOf(registry);
    for (const auto entity : registry.view<c::Planted, const c::Transform>())
    {
      auto& planted = registry.get<c::Planted>(entity);
      const auto& at = registry.get<c::Transform>(entity).position;
      for (Core::PlayerId player = 1; player < Core::kMaxPlayers; ++player)
        if (Vision::Visible(map, player, Physics::TileOf(at.x), Physics::TileOf(at.y)))
          planted.seen = static_cast<uint16_t>(planted.seen | (1u << player));
    }
  }
}
