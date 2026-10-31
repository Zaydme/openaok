#include "ecs/Vision.h"

#include <algorithm>

#include "core/Rules.h"
#include "ecs/BuildingGeometry.h"
#include "ecs/Diplomacy.h"
#include "ecs/Events.h"
#include "ecs/Flares.h"
#include "ecs/Players.h"
#include "ecs/components/Building.h"
#include "ecs/components/Doppelganger.h"
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
// The dat's "reveal enemy": Spies sets it. A player with it sees what every
// player it is not allied with sees now (ecs/Spies).
constexpr int kRevealEnemy = 183;

// For each player, the players whose sight it shares -- its allies, allied
// each way, if it has Cartography; everyone else in the game if it has Spies
// -- and those whose explored ground it knows: its allies, with Cartography.
// False when nobody shares anything, which is nearly always.
bool SharedSight(const entt::registry& registry, uint16_t sees[Core::kMaxPlayers],
                 uint16_t knows[Core::kMaxPlayers]) {
    bool any = false;
    for (int p = 1; p < Core::kMaxPlayers; ++p) {
        sees[p] = 0;
        knows[p] = 0;
        const Players::Player* seat = Players::Find(registry, static_cast<Core::PlayerId>(p));
        if (seat == nullptr) continue;
        const bool cartography = Players::Resource(*seat, kRevealAlly).value > 0;
        const bool spies = Players::Resource(*seat, kRevealEnemy).value > 0;
        if (!cartography && !spies) continue;
        for (int q = 1; q < Core::kMaxPlayers; ++q) {
            if (q == p || Players::Find(registry, static_cast<Core::PlayerId>(q)) == nullptr) continue;
            const uint16_t bit = Bit(static_cast<Core::PlayerId>(q));
            if (Diplomacy::Allied(registry, static_cast<Core::PlayerId>(p), static_cast<Core::PlayerId>(q))) {
                if (cartography) {
                    sees[p] = static_cast<uint16_t>(sees[p] | bit);
                    knows[p] = static_cast<uint16_t>(knows[p] | bit);
                }
            } else if (spies) {
                sees[p] = static_cast<uint16_t>(sees[p] | bit);
            }
        }
        any = any || sees[p] != 0;
    }
    return any;
}

// The players who gain a tile from a tile's mask, through who shares whose:
// the union of what the mask's low byte gives and what its high byte gives.
struct Gains {
    uint16_t low[256] = {};
    uint16_t high[256] = {};
    void From(const uint16_t shares[Core::kMaxPlayers]) {
        for (int p = 1; p < Core::kMaxPlayers; ++p) {
            if (shares[p] == 0) continue;
            const uint16_t bit = Bit(static_cast<Core::PlayerId>(p));
            for (int x = 1; x < 256; ++x) {
                if (x & (shares[p] & 0xFF)) low[x] = static_cast<uint16_t>(low[x] | bit);
                if (x & (shares[p] >> 8)) high[x] = static_cast<uint16_t>(high[x] | bit);
            }
        }
    }
    uint16_t Of(uint16_t mask) const { return static_cast<uint16_t>(mask | low[mask & 0xFF] | high[mask >> 8]); }
};

// Whether this stays drawn on fogged ground once found: what the dat marks
// so -- trees, mines, cliffs, never a building, which is remembered instead
// (ecs/Fog) -- and, for what the dat does not know, a map object that does
// not move.
bool StaysInFog(const entt::registry& registry, entt::entity entity) {
    if (const auto* identity = registry.try_get<Components::Identity>(entity)) {
        if (const auto* rules = Core::Rules::Unit(identity->unit_id)) return rules->fog_visibility != 0;
    }
    const auto* owner = registry.try_get<Components::Owner>(entity);
    const bool gaia = owner == nullptr || owner->player_id == Core::PLAYER_GAIA;
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

namespace {

// The tiles within `r` of (cx, cy), a row's run at a time, run(y, x0, x1):
// out from the middle row, each row out to the widest dx whose dx * dx +
// dy * dy stays within r * r, the same above and below the middle.
template <typename Run>
void Circle(const Map& map, int cx, int cy, int r, Run&& run) {
    const int r2 = r * r;
    int half = r;
    for (int dy = 0; dy <= r; ++dy) {
        while (half > 0 && half * half + dy * dy > r2) --half;
        const int x0 = std::max(0, cx - half);
        const int x1 = std::min(map.width - 1, cx + half);
        for (const int y : {cy - dy, cy + dy}) {
            if (y < 0 || y >= map.height || (dy == 0 && y != cy)) continue;
            run(y, x0, x1);
            if (dy == 0) break;
        }
    }
}

// The index of each tile a seer sees: its circle, or for a building that
// sees only its footprint, the footprint.
template <typename Visit>
void EachTile(const Map& map, const Map::Seer& seer, Visit&& visit) {
    if (!seer.footprint) {
        Circle(map, seer.x, seer.y, seer.radius, [&](int y, int x0, int x1) {
            const int row = Index(map, 0, y);
            for (int x = x0; x <= x1; ++x) visit(static_cast<size_t>(row + x));
        });
        return;
    }
    Buildings::ForEachFootprintTile(seer.unit_id, seer.x, seer.y, [&](int x, int y) {
        if (InBounds(map, x, y)) visit(static_cast<size_t>(Index(map, x, y)));
    });
}

std::vector<uint16_t>& Counts(Map& map, int player) {
    auto& counts = map.counts[static_cast<size_t>(player)];
    if (counts.empty()) counts.assign(map.own.size(), 0);
    return counts;
}

// A seer's tiles counted in, or out, for each player it lets see; a tile
// one comes to see is explored, and one nobody of a player's sees any more
// is out of its sight.
void Add(Map& map, const Map::Seer& seer) {
    for (int p = 0; p < 16; ++p) {
        if (((seer.players >> p) & 1u) == 0) continue;
        auto& counts = Counts(map, p);
        const uint16_t bit = Bit(static_cast<Core::PlayerId>(p));
        EachTile(map, seer, [&](size_t i) {
            if (counts[i]++ != 0) return;
            map.own[i] = static_cast<uint16_t>(map.own[i] | bit);
            map.visible[i] = static_cast<uint16_t>(map.visible[i] | bit);
            map.explored[i] = static_cast<uint16_t>(map.explored[i] | bit);
        });
    }
}

void Remove(Map& map, const Map::Seer& seer) {
    for (int p = 0; p < 16; ++p) {
        if (((seer.players >> p) & 1u) == 0) continue;
        auto& counts = Counts(map, p);
        const auto keep = static_cast<uint16_t>(~Bit(static_cast<Core::PlayerId>(p)));
        EachTile(map, seer, [&](size_t i) {
            if (--counts[i] != 0) return;
            map.own[i] = static_cast<uint16_t>(map.own[i] & keep);
            map.visible[i] = static_cast<uint16_t>(map.visible[i] & keep);
        });
    }
}

} // namespace

void Update(entt::registry& registry) {
    auto* map = registry.ctx().find<Map>();
    if (map == nullptr || map->reveal == Core::Reveal::AllVisible) return;
    const size_t tiles = static_cast<size_t>(map->width) * static_cast<size_t>(map->height);
    if (map->own.size() != tiles) {
        map->own.assign(tiles, 0);
        map->visible.assign(tiles, 0);
        map->counts.assign(16, {});
        map->slots.clear();
        map->stamped.clear();
        map->flares.clear();
        map->shared = false;
    }
    if (++map->pass == 0) map->pass = 1;
    const uint32_t pass = map->pass;

    // Each seer stamped again only when it has moved a tile or changed;
    // those gone, or garrisoned, counted out.
    const auto& identities = registry.storage<Components::Identity>();
    const auto& buildings = registry.storage<Components::Building>();
    auto seers = registry.view<const Components::Transform, const Components::Sight,
                               const Components::Owner>();
    for (auto e : seers) {
        const Core::PlayerId player = seers.get<const Components::Owner>(e).player_id;
        if (player == Core::PLAYER_GAIA || player >= 16) continue;
        const auto& at = seers.get<const Components::Transform>(e).position;
        const auto& sight = seers.get<const Components::Sight>(e);
        const bool identified = identities.contains(e);
        const Map::Seer seer{entt::to_integral(e), Physics::TileOf(at.x), Physics::TileOf(at.y), sight.radius,
                             Bit(player), identified ? identities.get(e).unit_id : -1,
                             sight.footprint && identified && buildings.contains(e)};
        const size_t index = static_cast<size_t>(entt::to_entity(e));
        if (index >= map->slots.size()) map->slots.resize(index + 1);
        Map::Slot& slot = map->slots[index];
        if (slot.pass != 0) {
            if (slot.seer == seer) {
                slot.pass = pass;
                continue;
            }
            Remove(*map, slot.seer);
        } else {
            map->stamped.push_back(static_cast<uint32_t>(index));
        }
        Add(*map, seer);
        slot.seer = seer;
        slot.pass = pass;
    }
    for (size_t i = 0; i < map->stamped.size();) {
        Map::Slot& slot = map->slots[map->stamped[i]];
        if (slot.pass == pass) {
            ++i;
            continue;
        }
        Remove(*map, slot.seer);
        slot.pass = 0;
        map->stamped[i] = map->stamped.back();
        map->stamped.pop_back();
    }
    // Treason's marks: two tiles round each King, for the side shown it.
    map->flares_now.clear();
    if (const Flares::Log* flares = Flares::Read(registry)) {
        const Events::Log* events = Events::Read(registry);
        const uint64_t now = events != nullptr ? events->tick : 0;
        for (const Flares::Flare& flare : flares->flares) {
            if (flare.sight <= 0 || !Flares::Showing(*flares, flare, now)) continue;
            map->flares_now.push_back(Map::Seer{0, Physics::TileOf(flare.at.x), Physics::TileOf(flare.at.y),
                                                flare.sight, flare.to, -1, false});
        }
    }
    if (map->flares_now != map->flares) {
        for (const auto& seer : map->flares) Remove(*map, seer);
        for (const auto& seer : map->flares_now) Add(*map, seer);
        map->flares.swap(map->flares_now);
    }
    // Shared sight: allies' for those with Cartography, enemies' for those
    // with Spies -- only what each sees and has explored itself, not what it
    // is shown in turn.
    uint16_t sees[Core::kMaxPlayers];
    uint16_t knows[Core::kMaxPlayers];
    if (SharedSight(registry, sees, knows)) {
        Gains seen;
        Gains known;
        seen.From(sees);
        known.From(knows);
        for (size_t i = 0; i < tiles; ++i) {
            map->visible[i] = seen.Of(map->own[i]);
            map->explored[i] = static_cast<uint16_t>(known.Of(map->explored[i]) | map->visible[i]);
        }
        map->shared = true;
    } else if (map->shared) {
        std::copy(map->own.begin(), map->own.end(), map->visible.begin());
        map->shared = false;
    }
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

const Map* MapOf(const entt::registry& registry) { return registry.ctx().find<Map>(); }

bool Visible(const entt::registry& registry, Core::PlayerId player, int x, int y) {
    return Visible(MapOf(registry), player, x, y);
}

bool Explored(const entt::registry& registry, Core::PlayerId player, int x, int y) {
    const auto* map = registry.ctx().find<Map>();
    if (map == nullptr) return true;
    if (!InBounds(*map, x, y)) return false;
    if (player < 16 && ((map->explore_all | map->see_all) & Bit(player)) != 0) return true;
    return (map->explored[static_cast<size_t>(Index(*map, x, y))] & Bit(player)) != 0;
}

bool Shows(const entt::registry& registry, Core::PlayerId viewer, entt::entity entity) {
    // A picture kept in the fog is its player's alone.
    if (const auto* picture = registry.try_get<Components::Doppelganger>(entity)) {
        return picture->player == viewer;
    }
    // A building's piece is shown as its building is.
    entt::entity subject = entity;
    if (const auto* part = registry.try_get<Components::PartOf>(entity)) {
        if (registry.valid(part->building)) subject = part->building;
    }
    if (const auto* owner = registry.try_get<Components::Owner>(subject)) {
        if (owner->player_id == viewer) return true;
        // A foundation not yet begun is shown to its owner's side alone.
        if (const auto* site = registry.try_get<Components::Building>(subject); site != nullptr && !site->begun) {
            return Diplomacy::Allied(registry, viewer, owner->player_id);
        }
    }
    const auto* at = registry.try_get<Components::Transform>(subject);
    if (at == nullptr) return true;
    const int x = Physics::TileOf(at->position.x);
    const int y = Physics::TileOf(at->position.y);
    if (Visible(registry, viewer, x, y)) return true;
    if (!Explored(registry, viewer, x, y)) return false;
    // In the fog, what the viewer keeps a picture of is the picture.
    if (const auto* remembered = registry.try_get<Components::Remembered>(subject);
        remembered != nullptr && viewer < 16 && (remembered->by & Bit(viewer)) != 0) {
        return false;
    }
    return StaysInFog(registry, subject);
}

} // namespace ECS::Vision
