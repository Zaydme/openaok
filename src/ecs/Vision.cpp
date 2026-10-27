#include "ecs/Vision.h"

#include <algorithm>

#include "core/Rules.h"
#include "ecs/Diplomacy.h"
#include "ecs/Players.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Owner.h"
#include "ecs/components/PartOf.h"
#include "ecs/components/Sight.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

namespace ECS::Vision {

namespace {

uint16_t Bit(Core::PlayerId player) {
    return static_cast<uint16_t>(1u << player);
}

int Index(const Map& map, int x, int y) {
    return y * map.width + x;
}

bool InBounds(const Map& map, int x, int y) {
    return x >= 0 && y >= 0 && x < map.width && y < map.height;
}

// The dat's "reveal ally": Cartography sets it. A player with it sees what
// its allies see and have explored; allies without it do not see the
// player's.
constexpr int kRevealAlly = 50;

// For each player, the players whose sight it shares: its allies -- allied
// each way -- if it has Cartography. False when nobody shares anything, which
// is nearly always.
bool SharedSight(const entt::registry& registry, uint16_t shared[Core::kMaxPlayers]) {
    bool any = false;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        shared[p] = 0;
        const Players::Player* seat = Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (seat == nullptr || Players::Resource(*seat, kRevealAlly).value <= 0) continue;
        for (int q = 1; q < Core::kMaxPlayers; ++q) {
            if (q == p || Players::Find(registry, static_cast<Core::PlayerId>(q)) == nullptr) continue;
            if (Diplomacy::Allied(registry, static_cast<Core::PlayerId>(p),
                                  static_cast<Core::PlayerId>(q))) {
                shared[p] = static_cast<uint16_t>(shared[p] | Bit(static_cast<Core::PlayerId>(q)));
            }
        }
        any = any || shared[p] != 0;
    }
    return any;
}

// Whether this unit stays seen on fogged ground once found: buildings, the
// map's own objects and what the dat marks, never other units.
bool StaysInFog(const entt::registry& registry, entt::entity entity) {
    if (registry.all_of<Components::Building>(entity)) return true;
    const auto* owner = registry.try_get<Components::Owner>(entity);
    const bool gaia = owner == nullptr || owner->player_id == Core::PLAYER_GAIA;
    if (const auto* identity = registry.try_get<Components::Identity>(entity)) {
        if (const auto* rules = Core::Rules::Unit(identity->unit_id)) {
            if (rules->fog_visibility != 0) return true;
        }
    }
    // A map object that does not move -- a tree, a mine -- stays where it was
    // seen; a living one does not.
    return gaia && !registry.all_of<Components::Health>(entity);
}

} // namespace

void Setup(entt::registry& registry, int width, int height, Core::Reveal reveal) {
    Map map;
    map.width = width;
    map.height = height;
    map.reveal = reveal;
    const size_t tiles = static_cast<size_t>(width) * static_cast<size_t>(height);
    map.explored.assign(tiles, reveal == Core::Reveal::Normal ? 0 : 0xFFFF);
    map.visible.assign(tiles, reveal == Core::Reveal::AllVisible ? 0xFFFF : 0);
    registry.ctx().insert_or_assign<Map>(std::move(map));
}

void Update(entt::registry& registry) {
    auto* map = registry.ctx().find<Map>();
    if (map == nullptr || map->reveal == Core::Reveal::AllVisible) return;
    std::fill(map->visible.begin(), map->visible.end(), 0);

    auto seers = registry.view<const Components::Transform, const Components::Sight,
                               const Components::Owner>();
    for (auto e : seers) {
        const Core::PlayerId player = seers.get<const Components::Owner>(e).player_id;
        if (player == Core::PLAYER_GAIA || player >= 16) continue;
        const uint16_t bit = Bit(player);
        const auto& at = seers.get<const Components::Transform>(e).position;
        const int cx = Physics::TileOf(at.x);
        const int cy = Physics::TileOf(at.y);
        const int r = seers.get<const Components::Sight>(e).radius;
        const int r2 = r * r;
        // Row by row, out from the middle row: a row's tiles within the radius
        // are one run, out to the widest dx whose dx * dx + dy * dy stays
        // within r * r, the same above and below the middle.
        int half = r;
        for (int dy = 0; dy <= r; ++dy) {
            while (half > 0 && half * half + dy * dy > r2) --half;
            const int x0 = std::max(0, cx - half);
            const int x1 = std::min(map->width - 1, cx + half);
            for (const int y : {cy - dy, cy + dy}) {
                if (y < 0 || y >= map->height || (dy == 0 && y != cy)) continue;
                uint16_t* row = &map->visible[static_cast<size_t>(Index(*map, 0, y))];
                for (int x = x0; x <= x1; ++x) row[x] |= bit;
                if (dy == 0) break;
            }
        }
    }
    // Allies' sight, for those with Cartography: only what each ally sees
    // and has explored itself, not what it is shown in turn.
    uint16_t shared[Core::kMaxPlayers];
    if (SharedSight(registry, shared)) {
        for (size_t i = 0; i < map->visible.size(); ++i) {
            const uint16_t sees = map->visible[i];
            const uint16_t knows = map->explored[i];
            uint16_t gains = 0;
            for (int p = 1; p < Core::kMaxPlayers; ++p) {
                if (shared[p] == 0) continue;
                if (sees & shared[p]) gains = static_cast<uint16_t>(gains | Bit(static_cast<Core::PlayerId>(p)));
                if (knows & shared[p]) {
                    map->explored[i] = static_cast<uint16_t>(map->explored[i] |
                                                             Bit(static_cast<Core::PlayerId>(p)));
                }
            }
            map->visible[i] = static_cast<uint16_t>(sees | gains);
        }
    }
    for (size_t i = 0; i < map->visible.size(); ++i) map->explored[i] |= map->visible[i];
}

void Explore(entt::registry& registry, Core::PlayerId player, int x, int y, int radius) {
    auto* map = registry.ctx().find<Map>();
    if (map == nullptr || player >= 16) return;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius || !InBounds(*map, x + dx, y + dy)) continue;
            uint16_t& tile = map->explored[static_cast<size_t>(Index(*map, x + dx, y + dy))];
            tile = static_cast<uint16_t>(tile | Bit(player));
        }
    }
}

void ToggleExploreAll(entt::registry& registry, Core::PlayerId player) {
    if (auto* map = registry.ctx().find<Map>(); map != nullptr && player < 16) {
        map->explore_all = static_cast<uint16_t>(map->explore_all ^ Bit(player));
    }
}

void ToggleSeeAll(entt::registry& registry, Core::PlayerId player) {
    if (auto* map = registry.ctx().find<Map>(); map != nullptr && player < 16) {
        map->see_all = static_cast<uint16_t>(map->see_all ^ Bit(player));
    }
}

bool Visible(const entt::registry& registry, Core::PlayerId player, int x, int y) {
    const auto* map = registry.ctx().find<Map>();
    if (map == nullptr) return true;
    if (!InBounds(*map, x, y)) return false;
    if (player < 16 && (map->see_all & Bit(player)) != 0) return true;
    return (map->visible[static_cast<size_t>(Index(*map, x, y))] & Bit(player)) != 0;
}

bool Explored(const entt::registry& registry, Core::PlayerId player, int x, int y) {
    const auto* map = registry.ctx().find<Map>();
    if (map == nullptr) return true;
    if (!InBounds(*map, x, y)) return false;
    if (player < 16 && ((map->explore_all | map->see_all) & Bit(player)) != 0) return true;
    return (map->explored[static_cast<size_t>(Index(*map, x, y))] & Bit(player)) != 0;
}

bool Shows(const entt::registry& registry, Core::PlayerId viewer, entt::entity entity) {
    // A building's piece is shown as its building is.
    entt::entity subject = entity;
    if (const auto* part = registry.try_get<Components::PartOf>(entity)) {
        if (registry.valid(part->building)) subject = part->building;
    }
    if (const auto* owner = registry.try_get<Components::Owner>(subject)) {
        if (owner->player_id == viewer) return true;
    }
    const auto* at = registry.try_get<Components::Transform>(subject);
    if (at == nullptr) return true;
    const int x = Physics::TileOf(at->position.x);
    const int y = Physics::TileOf(at->position.y);
    if (Visible(registry, viewer, x, y)) return true;
    return Explored(registry, viewer, x, y) && StaysInFog(registry, subject);
}

} // namespace ECS::Vision
