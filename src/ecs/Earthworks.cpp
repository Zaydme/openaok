#include "ecs/Earthworks.h"

#include <algorithm>
#include <vector>
#include "core/Cliffs.h"
#include "ecs/Fog.h"
#include "ecs/Navigation.h"
#include "ecs/Players.h"
#include "ecs/Flight.h"
#include "ecs/Vision.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Building.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Path.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/WorldJob.h"

namespace ECS::earthworks
{
  namespace
  {
    namespace c = Components;
    using Core::Fixed32;
    using Core::WorldJobKind;
    constexpr int ditchDepth = 2;
    constexpr int relocationReach = 32;

    // Описывает одну клетку подготовленного изменения рельефа.
    struct Cell
    {
      int x = 0, y = 0, level = 0;
    };
    // Хранит положение и радиус объекта при подборе свободных мест.
    struct Body
    {
      entt::entity entity = entt::null;
      c::Vector2D at{};
      Fixed32 radius{};
      bool isStatic = false;
      bool isTree = false;
      bool blocks = false;
      int tilesX = 1, tilesY = 1;
    };
    // Содержит проверенное перемещение без изменения сущности и её ресурсов.
    struct Relocation
    {
      Body before{};
      c::Vector2D after{};
    };

    template<typename Visit>
    void blockedTiles(const Body& body, c::Vector2D at, Visit visit)
    {
      const auto half = Fixed32(1) / Fixed32(2);
      const int x0 = Physics::TileOf(at.x - Fixed32(body.tilesX) * half + half);
      const int y0 = Physics::TileOf(at.y - Fixed32(body.tilesY) * half + half);
      for (int y = y0; y < y0 + body.tilesY; ++y)
        for (int x = x0; x < x0 + body.tilesX; ++x) visit(x, y);
    }

    bool blocksTile(const Body& body, int x, int y)
    {
      bool hasTile = false;
      if (body.blocks) blockedTiles(body, body.at, [&](int tx, int ty) { hasTile |= tx == x && ty == y; });
      return hasTile;
    }

    bool overlaps(const Buildings::Bounds& a, const Buildings::Bounds& b)
    {
      return a.min_x < b.max_x && a.max_x > b.min_x && a.min_y < b.max_y && a.max_y > b.min_y;
    }

    std::vector<Buildings::Bounds> buildings(const entt::registry& registry)
    {
      std::vector<Buildings::Bounds> result;
      for (const auto entity : registry.view<const c::Building, const c::Identity, const c::Transform>())
      {
        if (registry.any_of<c::Dying, c::Corpse>(entity)) continue;
        result.push_back(Buildings::FootprintBounds(registry.get<c::Transform>(entity).position,
          registry.get<c::Identity>(entity).unit_id));
      }
      return result;
    }

    bool isBuildingTile(const std::vector<Buildings::Bounds>& fixed, int x, int y)
    {
      const auto tile = Buildings::TileBounds({Fixed32(x), Fixed32(y)});
      return std::any_of(fixed.begin(), fixed.end(), [&](const auto& building) { return overlaps(tile, building); });
    }

    bool isTree(const entt::registry& registry, const Physics::Grid& grid, entt::entity entity)
    {
      if (const auto* resource = registry.try_get<c::Resource>(entity); resource && resource->type == Core::ResourceType::Wood)
        return true;
      if (const auto* job = registry.try_get<c::WorldJob>(entity))
        return job->kind == WorldJobKind::PlantTree && job->phase == c::WorldJobPhase::Growing;
      const auto& at = registry.get<c::Transform>(entity).position;
      return !registry.any_of<c::Animation, c::Health>(entity) && registry.all_of<c::Sprite>(entity) &&
        grid.GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).has_tree;
    }

    std::vector<Body> bodies(const entt::registry& registry, const Physics::Grid& grid)
    {
      std::vector<Body> result;
      for (const auto entity : registry.view<const c::Transform>())
      {
        if (registry.any_of<c::Building, c::PartOf, c::Dying, c::Corpse, c::Doppelganger>(entity)) continue;
        if (flight::isFlying(registry, entity)) continue;
        if (const auto* job = registry.try_get<c::WorldJob>(entity); job && job->phase == c::WorldJobPhase::Working) continue;
        const bool tree = isTree(registry, grid, entity);
        const auto& at = registry.get<c::Transform>(entity).position;
        const bool solid = registry.all_of<c::Sprite>(entity) &&
          grid.GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).blocked;
        if (!tree && !solid && !registry.any_of<c::Resource, c::Health, c::Animation>(entity)) continue;
        const auto* identity = registry.try_get<c::Identity>(entity);
        const auto* rules = identity ? Players::RulesFor(registry, entity, identity->unit_id) : nullptr;
        const bool isStatic = tree || !registry.all_of<c::Animation>(entity) || (rules && rules->speed.value <= 0);
        auto radius = std::max(Navigation::RadiusOf(registry, entity), Fixed32(1) / Fixed32(4));
        int tilesX = 1, tilesY = 1;
        const auto* sprite = registry.try_get<c::Sprite>(entity);
        if (identity && sprite && Core::Cliffs::PieceDrawn(identity->unit_id, sprite->slp_frame))
        {
          Core::Cliffs::Footprint(identity->unit_id, tilesX, tilesY);
          radius = std::max(radius, Fixed32(std::max(tilesX, tilesY)) / Fixed32(2));
        }
        result.push_back({entity, at, radius, isStatic, tree,
          isStatic && grid.GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).blocked, tilesX, tilesY});
      }
      std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.entity < b.entity; });
      return result;
    }

    int falloff(int rise) { return std::max(1, rise / 6); }

    std::vector<Cell> plan(const Physics::Grid& grid, const c::WorldJob& job,
      const std::vector<Buildings::Bounds>& fixed)
    {
      std::vector<Cell> cells;
      const int reach = radius(job.kind, job.rise);
      for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx)
        {
          const int x = job.tileX + dx, y = job.tileY + dy;
          if (!grid.InBounds(x, y) || isBuildingTile(fixed, x, y)) continue;
          const auto& tile = grid.GetTile(x, y);
          int level = job.targetElevation;
          if (job.kind == WorldJobKind::RaiseGround)
          {
            const int distance = std::max(std::abs(dx), std::abs(dy));
            const int base = job.targetElevation - job.rise;
            level = std::max(base + 1, job.targetElevation - distance * falloff(job.rise));
            if (level <= tile.elevation && !isWater(tile.terrain)) continue;
            level = std::max(level, tile.elevation);
          }
          else if (job.kind == WorldJobKind::RestoreGround)
          {
            if (tile.earthwork == Physics::Earthwork::None) continue;
            level = tile.originalElevation;
          }
          if (level < Physics::earthworkMinLevel || level > Physics::earthworkMaxLevel) continue;
          cells.push_back({x, y, level});
        }
      return cells;
    }

    bool relocationPlan(const entt::registry& registry, const Physics::Grid& grid,
      const std::vector<Cell>& cells, const std::vector<Buildings::Bounds>& fixed,
      std::vector<Relocation>& moves)
    {
      auto objects = bodies(registry, grid);
      // Reserve every pending work patch, so a shifted tree is not put straight
      // into the next section of the same mountain or ditch.
      std::vector<Buildings::Bounds> excluded;
      for (const auto site : registry.view<const c::WorldJob>())
      {
        const auto& job = registry.get<c::WorldJob>(site);
        if (Core::isEarthwork(job.kind)) excluded.push_back(bounds(job.tileX, job.tileY, job.kind, job.rise));
      }
      for (auto& body : objects)
      {
        const bool touched = std::any_of(cells.begin(), cells.end(), [&](const Cell& cell)
        {
          return bounds(cell.x, cell.y).DistanceFrom(body.at) < body.radius;
        });
        if (!touched) continue;
        const int oldX = Physics::TileOf(body.at.x), oldY = Physics::TileOf(body.at.y);
        const bool aquaticResource = body.isStatic && !body.isTree && isWater(grid.GetTile(oldX, oldY).terrain);
        bool hasDestination = false;
        c::Vector2D destination{};
        for (int ring = 1; ring <= relocationReach && !hasDestination; ++ring)
          for (int dy = -ring; dy <= ring && !hasDestination; ++dy)
            for (int dx = -ring; dx <= ring && !hasDestination; ++dx)
            {
              if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
              const int x = oldX + dx, y = oldY + dy;
              if (!grid.InBounds(x, y) || grid.IsBlocked(x, y)) continue;
              // Preserve sub-tile position, as ordinary unit movement does.
              const c::Vector2D candidate{body.at.x + Fixed32(dx), body.at.y + Fixed32(dy)};
              const auto& ground = grid.GetTile(x, y);
              if (body.isStatic ? (aquaticResource ? !isWater(ground.terrain) : !grid.IsWalkable(x, y) || isWater(ground.terrain)) :
                  !Navigation::CanStand(registry, grid, body.entity, candidate)) continue;
              if (body.blocks)
              {
                bool isClear = true;
                blockedTiles(body, candidate, [&](int tx, int ty)
                {
                  if (!grid.InBounds(tx, ty) || grid.IsBlocked(tx, ty) ||
                      (aquaticResource ? !isWater(grid.GetTile(tx, ty).terrain) :
                        !grid.IsWalkable(tx, ty) || isWater(grid.GetTile(tx, ty).terrain))) isClear = false;
                });
                if (!isClear) continue;
              }
              if (std::any_of(excluded.begin(), excluded.end(), [&](const auto& patch) { return patch.DistanceFrom(candidate) <= body.radius; }) ||
                  std::any_of(fixed.begin(), fixed.end(), [&](const auto& building) { return building.DistanceFrom(candidate) < body.radius; })) continue;
              const bool occupied = std::any_of(objects.begin(), objects.end(), [&](const Body& other)
              {
                if (other.entity == body.entity) return false;
                const auto distance = body.radius + other.radius;
                return std::abs((candidate.x - other.at.x).value) < distance.value &&
                  std::abs((candidate.y - other.at.y).value) < distance.value;
              });
              if (occupied) continue;
              destination = candidate;
              hasDestination = true;
            }
        if (!hasDestination) return false;
        moves.push_back({body, destination});
        body.at = destination;
      }
      return true;
    }
  }

  bool isWater(Core::TerrainType terrain)
  {
    return terrain == Core::TerrainType::DeepWater || terrain == Core::TerrainType::MediumWater ||
      terrain == Core::TerrainType::ShallowWater || terrain == Core::TerrainType::Swamp;
  }

  int radius(Core::WorldJobKind kind, int rise)
  {
    return kind == WorldJobKind::RaiseGround && rise > 1 ? std::min(6, rise / falloff(rise)) : 0;
  }

  Buildings::Bounds bounds(int x, int y, Core::WorldJobKind kind, int rise)
  {
    auto result = Buildings::TileBounds({Fixed32(x), Fixed32(y)});
    const Fixed32 margin(radius(kind, rise) + 1);
    result.min_x = result.min_x - margin;
    result.min_y = result.min_y - margin;
    result.max_x = result.max_x + margin;
    result.max_y = result.max_y + margin;
    return result;
  }

  int targetLevel(const Physics::Tile& tile, Core::WorldJobKind kind, int levels)
  {
    if (kind == WorldJobKind::RaiseGround)
      return std::min(Physics::earthworkMaxLevel, tile.elevation + std::clamp(levels, 1, Physics::earthworkMaxLevel));
    if (kind == WorldJobKind::DigLake || Core::isWaterwork(kind)) return -ditchDepth;
    if (kind == WorldJobKind::DigDitch)
      return std::max(Physics::earthworkMinLevel, tile.elevation - ditchDepth);
    if (kind == WorldJobKind::DigChannel)
      return std::max(Physics::earthworkMinLevel, tile.elevation - 1);
    return tile.originalElevation;
  }

  bool canWork(const entt::registry& registry, const Physics::Grid& grid,
    Core::PlayerId player, int x, int y, Core::WorldJobKind kind, int levels, entt::entity ownSite)
  {
    if (player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers || !Core::isEarthwork(kind) ||
        !grid.InBounds(x, y) || !Vision::Explored(registry, player, x, y)) return false;
    const auto& tile = grid.GetTile(x, y);
    if (tile.elevation < Physics::earthworkMinLevel || tile.elevation > Physics::earthworkMaxLevel) return false;
    if (kind == WorldJobKind::RestoreGround && tile.earthwork == Physics::Earthwork::None) return false;
    if (targetLevel(tile, kind, levels) == tile.elevation)
    {
      if (kind == WorldJobKind::RaiseGround && !isWater(tile.terrain)) return false;
      if (kind == WorldJobKind::DigLake && tile.earthwork == Physics::Earthwork::Lake) return false;
      if (kind == WorldJobKind::DigRiver && tile.earthwork == Physics::Earthwork::River) return false;
      if (kind == WorldJobKind::DigWaterMoat && tile.earthwork == Physics::Earthwork::WaterMoat) return false;
      if (kind == WorldJobKind::DigDitch && tile.earthwork == Physics::Earthwork::Ditch && !isWater(tile.terrain)) return false;
      if (kind == WorldJobKind::DigChannel && tile.earthwork == Physics::Earthwork::Channel && !isWater(tile.terrain)) return false;
    }
    const auto fixed = buildings(registry);
    if (isBuildingTile(fixed, x, y)) return false;
    for (const auto other : registry.view<const c::WorldJob>())
    {
      if (other == ownSite) continue;
      const auto& job = registry.get<c::WorldJob>(other);
      if (job.tileX == x && job.tileY == y && job.phase == c::WorldJobPhase::Working) return false;
    }
    c::WorldJob job;
    job.kind = kind; job.tileX = x; job.tileY = y;
    job.rise = std::max(1, levels); job.targetElevation = targetLevel(tile, kind, levels);
    for (const auto& cell : plan(grid, job, fixed))
      if (!Vision::Explored(registry, player, cell.x, cell.y)) return false;
    return true;
  }

  bool finish(entt::registry& registry, Physics::Grid& grid, entt::entity site)
  {
    const auto job = registry.get<c::WorldJob>(site);
    const auto fixed = buildings(registry);
    if (isBuildingTile(fixed, job.tileX, job.tileY)) return false;
    const auto cells = plan(grid, job, fixed);
    if (cells.empty()) return true;
    std::vector<Relocation> moves;
    if (!relocationPlan(registry, grid, cells, fixed, moves)) return false;
    for (const auto& cell : cells)
      if (grid.GetTile(cell.x, cell.y).blocked && std::none_of(moves.begin(), moves.end(), [&](const Relocation& move)
      {
        return blocksTile(move.before, cell.x, cell.y);
      })) return false;
    // No world mutation until destinations for every displaced object exist.
    std::vector<std::pair<int, int>> freed;
    for (const auto& move : moves)
    {
      const auto entity = move.before.entity;
      if (move.before.isStatic)
      {
        Fog::Going(registry, entity);
        registry.get_or_emplace<c::Planted>(entity).seen = 0;
      }
      auto& at = registry.get<c::Transform>(entity);
      at.previous_position = at.position;
      at.position = move.after;
      registry.remove<c::Path>(entity);
      if (auto* treeJob = registry.try_get<c::WorldJob>(entity))
      {
        treeJob->tileX = Physics::TileOf(move.after.x);
        treeJob->tileY = Physics::TileOf(move.after.y);
      }
      if (move.before.blocks)
      {
        blockedTiles(move.before, move.before.at, [&](int x, int y) { freed.emplace_back(x, y); });
        blockedTiles(move.before, move.after, [&](int x, int y)
        {
          grid.GetTileMut(x, y).has_tree = move.before.isTree;
          grid.SetWalkable(x, y, false);
        });
      }
    }
    const auto remaining = bodies(registry, grid);
    for (const auto& [x, y] : freed)
    {
      if (!grid.InBounds(x, y) || isBuildingTile(fixed, x, y) ||
          std::any_of(remaining.begin(), remaining.end(), [&](const Body& body) { return blocksTile(body, x, y); })) continue;
      grid.GetTileMut(x, y).has_tree = false;
      grid.SetWalkable(x, y, true);
    }
    // Keep the shared vertices at a neighbouring building's original height.
    // Capture all of them before raising any cell in the mountain's footprint.
    for (const auto& cell : cells)
    {
      Fog::GroundChanging(registry, cell.x, cell.y, grid.GetTile(cell.x, cell.y));
      uint8_t mask = 0;
      for (int corner = 0; corner < 4; ++corner)
      {
        const c::Vector2D vertex{Fixed32(cell.x + corner % 2) - Fixed32(1) / Fixed32(2),
          Fixed32(cell.y + corner / 2) - Fixed32(1) / Fixed32(2)};
        if (std::any_of(fixed.begin(), fixed.end(), [&](const auto& building) { return building.DistanceFrom(vertex).value == 0; }))
          mask = static_cast<uint8_t>(mask | (1 << corner));
      }
      grid.PinCorners(cell.x, cell.y, mask);
    }
    const auto kind = job.kind == WorldJobKind::RaiseGround ? Physics::Earthwork::Raised :
      job.kind == WorldJobKind::DigDitch ? Physics::Earthwork::Ditch :
      job.kind == WorldJobKind::DigChannel ? Physics::Earthwork::Channel :
      job.kind == WorldJobKind::DigRiver ? Physics::Earthwork::River :
      job.kind == WorldJobKind::DigWaterMoat ? Physics::Earthwork::WaterMoat :
      job.kind == WorldJobKind::DigLake ? Physics::Earthwork::Lake : Physics::Earthwork::None;
    for (const auto& cell : cells) grid.SetEarthwork(cell.x, cell.y, cell.level, kind, job.m_flowDirection);
    return true;
  }
}
