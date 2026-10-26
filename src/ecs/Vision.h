#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/Types.h"

// What each player can see, and has seen.
//
// Every unit and building sees a circle of its line of sight; a tile inside
// any of its owner's circles is visible to that player now, and once visible
// it stays explored. A player with Cartography (the dat's "reveal ally",
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
bool Explored(const entt::registry& registry, Core::PlayerId player, int x, int y);

// Whether `viewer` sees `entity` drawn: its own things always; anything on a
// tile it can see now; and on a tile it has only explored, what the dat says
// stays shown in the fog -- buildings and the map's own objects -- but never
// another player's units.
bool Shows(const entt::registry& registry, Core::PlayerId viewer, entt::entity entity);

} // namespace ECS::Vision
