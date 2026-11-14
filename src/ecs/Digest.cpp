#include "ecs/Digest.h"

#include <algorithm>
#include <vector>

#include "core/Rules.h"
#include "core/Random.h"
#include "ecs/Simulation.h"
#include "ecs/components/Resource.h"
#include "ecs/Cheats.h"
#include "ecs/Diplomacy.h"
#include "ecs/Market.h"
#include "ecs/Players.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Transform.h"
#include "ecs/components/WorldJob.h"
#include "ecs/Fog.h"
#include "ecs/components/Worker.h"
#include "ecs/components/Projectile.h"

namespace ECS::Digest {

uint64_t Of(const entt::registry& registry) {
    // FNV-1a.
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            hash ^= static_cast<uint8_t>(v >> (b * 8));
            hash *= 1099511628211ull;
        }
    };
    std::vector<entt::entity> all;
    for (auto e : registry.view<const Components::Identity>()) all.push_back(e);
    std::sort(all.begin(), all.end());
    for (entt::entity e : all) {
        mix(static_cast<uint64_t>(entt::to_integral(e)));
        mix(static_cast<uint64_t>(registry.get<Components::Identity>(e).unit_id));
        if (const auto* t = registry.try_get<Components::Transform>(e)) {
            mix(static_cast<uint64_t>(t->position.x.value));
            mix(static_cast<uint64_t>(t->position.y.value));
        }
        if (const auto* h = registry.try_get<Components::Health>(e)) {
            mix(static_cast<uint64_t>(h->current_hp));
        }
        if (const auto* o = registry.try_get<Components::Owner>(e)) mix(o->player_id);
    }
    for (int p = 0; p < Core::kMaxPlayers; ++p) {
        const auto player = static_cast<Core::PlayerId>(p);
        const Players::Player* seat = Players::Find(registry, player);
        if (seat == nullptr) continue;
        for (int r : {Core::Rules::Res::Food, Core::Rules::Res::Wood, Core::Rules::Res::Stone,
                      Core::Rules::Res::Gold}) {
            mix(static_cast<uint64_t>(Players::Resource(*seat, r).value));
        }
        for (int q = 0; q < Core::kMaxPlayers; ++q) {
            mix(static_cast<uint64_t>(Diplomacy::StanceOf(registry, player,
                                                          static_cast<Core::PlayerId>(q))));
        }
        mix(Diplomacy::AlliedVictory(registry, player) ? 1u : 0u);
    }
    for (int r : {Core::Rules::Res::Food, Core::Rules::Res::Wood, Core::Rules::Res::Stone}) {
        mix(static_cast<uint64_t>(Market::BasePrice(registry, r)));
    }
    // The cheats left on: none, until one is typed.
    const auto* toggles = registry.ctx().find<Cheats::Toggles>();
    mix(toggles != nullptr ? toggles->aegis : 0u);
    mix(toggles != nullptr ? toggles->nature : 0u);
    all.clear();
    for (const auto entity : registry.view<const Components::Projectile>()) all.push_back(entity);
    std::sort(all.begin(), all.end());
    for (const auto entity : all)
    {
      const auto& shot = registry.get<Components::Projectile>(entity);
      mix(entt::to_integral(entity));
      mix(entt::to_integral(shot.shooter)); mix(entt::to_integral(shot.target));
      mix(shot.owner); mix(shot.unit_id);
      mix(shot.from.x.value); mix(shot.from.y.value); mix(shot.to.x.value); mix(shot.to.y.value);
      mix(shot.length.value); mix(shot.travelled.value); mix(shot.speed.value); mix(shot.arc.value);
      mix(shot.m_fromLift); mix(shot.m_toLift); mix(shot.on_target);
      mix(shot.attack_count); mix(shot.blast_width.value); mix(shot.blast_level);
      for (int i = 0; i < shot.attack_count; ++i) { mix(shot.attacks[i].cls); mix(shot.attacks[i].amount); }
    }
    return hash;
}

uint64_t Of(const Simulation& simulation)
{
  const auto& registry = simulation.GetRegistry();
  uint64_t hash = Of(registry);
  const auto mix = [&hash](uint64_t value)
  {
    for (int shift = 0; shift < 64; shift += 8)
      hash = (hash ^ static_cast<uint8_t>(value >> shift)) * 1099511628211ull;
  };
  mix(simulation.Tick());
  const auto& grid = simulation.GetGrid();
  mix(grid.GetWidth());
  mix(grid.GetHeight());
  for (const auto& tile : grid.Tiles())
  {
    mix(static_cast<uint32_t>(tile.terrain) |
      (static_cast<uint64_t>(static_cast<uint32_t>(tile.elevation)) << 32));
    mix(static_cast<uint64_t>(tile.blocked) |
      (static_cast<uint64_t>(tile.has_tree) << 1) |
      (static_cast<uint64_t>(tile.is_walkable) << 2) |
      (static_cast<uint64_t>(tile.passable) << 8) |
      (static_cast<uint64_t>(tile.gate) << 16));
    mix(static_cast<uint8_t>(tile.earthwork));
    mix(tile.originalElevation);
    mix(static_cast<uint8_t>(tile.originalTerrain));
    mix(tile.earthBlocked);
    mix(tile.pinnedCorners);
    for (const auto corner : tile.cornerHeights) mix(corner);
    mix(tile.m_flowDirection);
  }
  std::vector<entt::entity> nodes;
  for (const auto entity : registry.view<const Components::Resource>()) nodes.push_back(entity);
  std::sort(nodes.begin(), nodes.end());
  for (const auto entity : nodes)
  {
    const auto& resource = registry.get<Components::Resource>(entity);
    mix(entt::to_integral(entity));
    mix(static_cast<uint64_t>(resource.type));
    mix(static_cast<uint64_t>(resource.current_amount));
  }
  if (const auto* random = registry.ctx().find<Core::Random>())
  {
    auto sample = *random;
    mix(sample.Next());
  }
  nodes.clear();
  for (const auto entity : registry.view<const Components::WorldJob>()) nodes.push_back(entity);
  std::sort(nodes.begin(), nodes.end());
  mix(nodes.size());
  for (const auto entity : nodes)
  {
    const auto& job = registry.get<Components::WorldJob>(entity);
    mix(entt::to_integral(entity));
    mix(static_cast<uint8_t>(job.kind));
    mix(static_cast<uint8_t>(job.phase));
    mix(job.tileX); mix(job.tileY); mix(job.paidWood); mix(job.workMs);
    mix(job.workedMs.value); mix(job.growthLeftMs); mix(job.hasBlocked);
    mix(job.targetElevation);
    mix(job.rise); mix(job.clearanceWaitMs);
    mix(job.paidGold);
    mix(job.m_flowDirection);
  }
  nodes.clear();
  for (const auto entity : registry.view<const Components::WorldJobQueue>()) nodes.push_back(entity);
  std::sort(nodes.begin(), nodes.end());
  mix(nodes.size());
  for (const auto entity : nodes)
  {
    const auto& queue = registry.get<Components::WorldJobQueue>(entity);
    mix(entt::to_integral(entity));
    mix(queue.count);
    for (uint16_t i = 0; i < queue.count; ++i) mix(entt::to_integral(queue.sites[i]));
    if (const auto* worker = registry.try_get<Components::Worker>(entity))
    {
      mix(static_cast<uint8_t>(worker->state)); mix(worker->working);
      mix(entt::to_integral(worker->target_building)); mix(worker->build_attempts);
    }
  }
  nodes.clear();
  for (const auto entity : registry.view<const Components::Planted>()) nodes.push_back(entity);
  std::sort(nodes.begin(), nodes.end());
  mix(nodes.size());
  for (const auto entity : nodes)
  {
    mix(entt::to_integral(entity));
    mix(registry.get<Components::Planted>(entity).seen);
    if (const auto* at = registry.try_get<Components::Transform>(entity))
    {
      mix(at->position.x.value); mix(at->position.y.value);
    }
  }
  if (const auto* fog = Fog::Read(registry))
    for (const auto& memory : fog->ground)
    {
      mix(memory.size());
      for (const auto& tile : memory)
      {
        mix(tile.tile);
        mix(static_cast<uint8_t>(tile.terrain));
        mix(tile.elevation);
        mix(tile.pinnedCorners);
        for (const auto corner : tile.cornerHeights) mix(corner);
        mix(tile.m_flowDirection);
      }
    }
  return hash;
}

} // namespace ECS::Digest
