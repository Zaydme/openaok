#include "ecs/Fog.h"

#include <algorithm>
#include <utility>

#include "ecs/BuildingGeometry.h"
#include "ecs/Players.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/components/Identity.h"
#include "ecs/components/MinimapMarker.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/SpritePieces.h"
#include "ecs/components/Transform.h"

namespace ECS::Fog {

namespace {

namespace C = Components;

// How many changes to the remembered ground the log keeps for its reader.
constexpr size_t kLogSize = 1024;

uint16_t Bit(Core::PlayerId player) { return static_cast<uint16_t>(1u << player); }

int Width(const entt::registry& registry) {
    const auto* map = registry.ctx().find<Vision::Map>();
    return map != nullptr ? map->width : 0;
}

// The players who see the tile now.
uint16_t SeenBy(const entt::registry& registry, int x, int y) {
    uint16_t mask = 0;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        if (Vision::Visible(registry, p, x, y)) mask = static_cast<uint16_t>(mask | Bit(p));
    }
    return mask;
}

void Logged(Memory& memory, int x, int y) {
    memory.log.push_back({++memory.revision, x, y});
    if (memory.log.size() > kLogSize) memory.log.erase(memory.log.begin());
}

// Forgets `player`'s picture of `source`.
void Forget(entt::registry& registry, Core::PlayerId player, entt::entity source) {
    std::vector<entt::entity> pieces;
    auto pictures = registry.view<const C::Doppelganger>();
    for (auto e : pictures) {
        const auto& d = pictures.get<const C::Doppelganger>(e);
        if (d.player == player && d.source == source) pieces.push_back(e);
    }
    for (entt::entity e : pieces) registry.destroy(e);
    if (registry.valid(source)) {
        if (auto* remembered = registry.try_get<C::Remembered>(source)) {
            remembered->by = static_cast<uint16_t>(remembered->by & ~Bit(player));
            if (remembered->by == 0) registry.remove<C::Remembered>(source);
        }
    }
}

// `players`' picture of `thing`: each piece of it -- itself and, for a
// building, its parts -- as drawn now, still.
void Picture(entt::registry& registry, entt::entity thing, uint16_t players) {
    std::vector<entt::entity> pieces;
    if (registry.all_of<C::Sprite, C::Transform>(thing)) pieces.push_back(thing);
    auto parts = registry.view<const C::PartOf, const C::Sprite, const C::Transform>();
    for (auto e : parts) {
        if (parts.get<const C::PartOf>(e).building == thing) pieces.push_back(e);
    }
    if (pieces.empty() || !registry.all_of<C::Transform>(thing)) return;
    const auto* owner = registry.try_get<C::Owner>(thing);
    const auto* identity = registry.try_get<C::Identity>(thing);
    const Core::PlayerId colour = owner != nullptr ? owner->player_id : Core::PLAYER_GAIA;
    const int unit_id = identity != nullptr ? identity->unit_id : -1;
    const C::Vector2D origin = registry.get<C::Transform>(thing).position;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        if ((players & Bit(p)) == 0) continue;
        Forget(registry, p, thing);
        for (size_t i = 0; i < pieces.size(); ++i) {
            const entt::entity piece = pieces[i];
            const C::Vector2D at = registry.get<C::Transform>(piece).position;
            C::Sprite sprite = registry.get<C::Sprite>(piece);
            sprite.still = true;
            const auto* split = registry.try_get<C::SpritePieces>(piece);
            const C::SpritePieces split_copy = split != nullptr ? *split : C::SpritePieces{};
            const auto* marker = registry.try_get<C::MinimapMarker>(piece);
            const C::MinimapMarker marker_copy = marker != nullptr ? *marker : C::MinimapMarker{};
            const bool has_split = split != nullptr;
            const bool has_marker = marker != nullptr;
            const entt::entity copy = registry.create();
            registry.emplace<C::Transform>(copy, C::Transform{at, at});
            registry.emplace<C::Sprite>(copy, sprite);
            if (has_split) registry.emplace<C::SpritePieces>(copy, split_copy);
            if (has_marker) registry.emplace<C::MinimapMarker>(copy, marker_copy);
            registry.emplace<C::Doppelganger>(copy, C::Doppelganger{p, colour, thing, unit_id, origin, i == 0});
        }
        auto& remembered = registry.get_or_emplace<C::Remembered>(thing);
        remembered.by = static_cast<uint16_t>(remembered.by | Bit(p));
    }
}

} // namespace

void Setup(entt::registry& registry) {
    registry.ctx().insert_or_assign(Memory{});
}

void Update(entt::registry& registry) {
    Memory* memory = registry.ctx().find<Memory>();
    if (memory == nullptr) return;

    // Buildings players stop seeing: the tile under the middle, by everyone
    // but the owner.
    std::vector<std::pair<entt::entity, uint16_t>> lost;
    auto buildings = registry.view<C::Building, const C::Transform, const C::Owner>();
    for (auto b : buildings) {
        auto& state = buildings.get<C::Building>(b);
        if (!state.begun) continue;
        const auto& at = buildings.get<const C::Transform>(b).position;
        const uint16_t now = SeenBy(registry, Physics::TileOf(at.x), Physics::TileOf(at.y));
        const uint16_t owner = Bit(buildings.get<const C::Owner>(b).player_id);
        const auto gone = static_cast<uint16_t>(state.seen & ~now & ~owner);
        state.seen = now;
        if (gone != 0) lost.emplace_back(b, gone);
    }
    for (const auto& [building, players] : lost) Picture(registry, building, players);

    // Pictures whose ground their player sees again.
    std::vector<std::pair<Core::PlayerId, entt::entity>> seen;
    auto pictures = registry.view<const C::Doppelganger>();
    for (auto e : pictures) {
        const auto& d = pictures.get<const C::Doppelganger>(e);
        if (!d.main) continue;
        bool sees = false;
        Buildings::ForEachFootprintTile(d.unit_id, Physics::TileOf(d.origin.x), Physics::TileOf(d.origin.y),
                                        [&](int x, int y) {
            sees = sees || Vision::Visible(registry, d.player, x, y);
        });
        if (sees) seen.emplace_back(d.player, d.source);
    }
    for (const auto& [player, source] : seen) Forget(registry, player, source);

    // Remembered ground seen again.
    const int width = Width(registry);
    if (width <= 0) return;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        std::erase_if(memory->ground[p], [&](const Ground& g) {
            const int x = g.tile % width;
            const int y = g.tile / width;
            if (!Vision::Visible(registry, p, x, y)) return false;
            Logged(*memory, x, y);
            return true;
        });
    }
}

void Going(entt::registry& registry, entt::entity thing) {
    const auto* at = registry.try_get<C::Transform>(thing);
    if (at == nullptr) return;
    const int x = Physics::TileOf(at->position.x);
    const int y = Physics::TileOf(at->position.y);
    const auto* remembered = registry.try_get<C::Remembered>(thing);
    uint16_t players = 0;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        if (Players::Find(registry, p) == nullptr) continue;
        if (!Vision::Explored(registry, p, x, y) || Vision::Visible(registry, p, x, y)) continue;
        if (remembered != nullptr && (remembered->by & Bit(p)) != 0) continue;
        players = static_cast<uint16_t>(players | Bit(p));
    }
    if (players != 0) Picture(registry, thing, players);
}

void GroundChanging(entt::registry& registry, int x, int y, Core::TerrainType was) {
    Memory* memory = registry.ctx().find<Memory>();
    const int width = Width(registry);
    if (memory == nullptr || width <= 0) return;
    const int32_t tile = y * width + x;
    for (Core::PlayerId p = 1; p < Core::kMaxPlayers; ++p) {
        if (Players::Find(registry, p) == nullptr) continue;
        if (!Vision::Explored(registry, p, x, y) || Vision::Visible(registry, p, x, y)) continue;
        auto& list = memory->ground[p];
        auto it = std::lower_bound(list.begin(), list.end(), tile,
                                   [](const Ground& g, int32_t t) { return g.tile < t; });
        if (it != list.end() && it->tile == tile) continue;
        list.insert(it, Ground{tile, was});
        Logged(*memory, x, y);
    }
}

Core::TerrainType GroundSeen(const entt::registry& registry, Core::PlayerId viewer, int x, int y,
                             Core::TerrainType now) {
    const Memory* memory = registry.ctx().find<Memory>();
    if (memory == nullptr || viewer >= Core::kMaxPlayers || memory->ground[viewer].empty()) return now;
    const int32_t tile = y * Width(registry) + x;
    const auto& list = memory->ground[viewer];
    auto it = std::lower_bound(list.begin(), list.end(), tile,
                               [](const Ground& g, int32_t t) { return g.tile < t; });
    return it != list.end() && it->tile == tile ? it->terrain : now;
}

const Memory* Read(const entt::registry& registry) {
    return registry.ctx().find<Memory>();
}

} // namespace ECS::Fog
