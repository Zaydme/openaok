#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"

// What each player can see, and has seen.
//
// Every unit and building sees a circle of its line of sight -- a building
// not yet finished only the ground under itself; a tile inside any of its
// owner's circles is visible to that player now, and once visible it stays
// explored. A player with Cartography (the dat's "reveal ally",
// resource 50) also sees what its allies see, and knows what they have
// explored. Kept per player in the simulation, because what a player can see
// is part of the game -- the AI plays from it -- and every machine has to
// agree on it. The interface only reads it, for whoever is sitting at this
// one.
namespace ECS::Vision {

struct Map {
    int width = 0;
    int height = 0;
    Core::Reveal reveal = Core::Reveal::Normal;
    // One bit per player, per tile.
    std::vector<uint16_t> explored;
    std::vector<uint16_t> visible;
    // Players the cheats have shown the whole map to: explored ("marco") or
    // all in sight ("polo"), a bit each (ecs/Cheats).
    uint16_t explore_all = 0;
    uint16_t see_all = 0;

    // What `visible` is made from, kept between ticks so that a tick stamps
    // again only the seers that have moved a tile or changed: for each
    // player, how many seers see each tile; what each player sees itself,
    // before shared sight; and each seer as it was stamped. Made afresh when
    // empty, as after loading a game.
    struct Seer {
        uint32_t entity = 0;
        int x = 0;
        int y = 0;
        int radius = 0;
        uint16_t players = 0;      // a bit per player it lets see
        int unit_id = -1;          // the building, for one that sees its footprint
        bool footprint = false;
        bool operator==(const Seer&) const = default;
    };
    struct Slot {
        Seer seer;
        uint32_t pass = 0;         // the last Update that found it; 0 when not stamped
    };
    std::vector<std::vector<uint16_t>> counts;   // [player][tile]
    std::vector<uint16_t> own;
    std::vector<Slot> slots;                     // by entity index
    std::vector<uint32_t> stamped;               // the slots stamped
    std::vector<Seer> flares;                    // Treason's marks as stamped
    std::vector<Seer> flares_now;                // this tick's, compared with the above
    uint32_t pass = 0;
    bool shared = false;                         // whether `visible` was last made with shared sight
};

// `reveal` is the lobby's choice: fog as normal, the map explored from the
// start, or everything visible.
void Setup(entt::registry& registry, int width, int height, Core::Reveal reveal);

// Recomputes what every player sees now.
void Update(entt::registry& registry);

// Marks a circle of ground explored for `player`, as though it had been
// seen: a team-mate's Town Center at the start of a game.
void Explore(entt::registry& registry, Core::PlayerId player, int x, int y, int radius);
// The cheats' toggles: `player` sees the whole map explored, or all of it in
// sight, until toggled back.
void ToggleExploreAll(entt::registry& registry, Core::PlayerId player);
void ToggleSeeAll(entt::registry& registry, Core::PlayerId player);

bool Visible(const entt::registry& registry, Core::PlayerId player, int x, int y);
// The same from the map, found once by a loop over many tiles (null when
// there is none, and then everything is visible).
const Map* MapOf(const entt::registry& registry);
inline bool Visible(const Map* map, Core::PlayerId player, int x, int y) {
    if (map == nullptr) return true;
    if (x < 0 || y < 0 || x >= map->width || y >= map->height) return false;
    const auto bit = static_cast<uint16_t>(1u << player);
    if (player < 16 && (map->see_all & bit) != 0) return true;
    return (map->visible[static_cast<size_t>(y * map->width + x)] & bit) != 0;
}
bool Explored(const entt::registry& registry, Core::PlayerId player, int x, int y);
inline bool Explored(const Map* map, Core::PlayerId player, int x, int y) {
    if (map == nullptr) return true;
    if (x < 0 || y < 0 || x >= map->width || y >= map->height) return false;
    const auto bit = static_cast<uint16_t>(1u << player);
    if (player < 16 && ((map->explore_all | map->see_all) & bit) != 0) return true;
    return (map->explored[static_cast<size_t>(y * map->width + x)] & bit) != 0;
}

// Whether `viewer` sees `entity` drawn: its own things always; a picture of
// the fog's (ecs/Fog) if it is the viewer's; a foundation not yet begun only
// if it is its owner's ally each way; anything else on a tile it can see now;
// and on a tile it has only explored, what the dat keeps shown in the fog --
// trees, mines, cliffs, never a building or another player's unit -- unless
// the viewer keeps a picture of it.
bool Shows(const entt::registry& registry, Core::PlayerId viewer, entt::entity entity);

} // namespace ECS::Vision
