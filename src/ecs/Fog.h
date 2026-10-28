#pragma once
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "core/GameSetup.h"
#include "core/Types.h"
#include "physics/Grid.h"

// What each player remembers of what it has seen and does not see now, as the
// original keeps it with its doppelgangers.
//
// A building is never drawn in the fog itself (the dat's fog visibility 0).
// When a player stops seeing a building that has been begun -- the tile under
// its middle -- it keeps a picture of it as it stands then, every piece of it
// as drawn, in its owner's colour; the picture stays, whatever becomes of the
// building, until the player sees any tile of its ground again, and a
// building put up out of the player's sight is not drawn for it at all. Map
// objects the fog keeps shown (a mine, a bush, a tree) are drawn as they are;
// one cut down or used up while a player who has explored its tile does not
// see it leaves that player a picture of it as it was.
//
// The ground likewise: terrain a building lays (its foundation's, a farm's
// stages) on a tile a player has explored and does not see stays, for that
// player, as it last saw it until it looks again.
//
// Kept in the simulation, every player's alike; the interface draws the
// player at this machine's.
namespace ECS::Fog {

struct Ground {
    int32_t tile = 0;                   // y * width + x
    Core::TerrainType terrain{};        // as the player last saw it
};

struct Memory {
    std::vector<Ground> ground[Core::kMaxPlayers];   // by tile, ascending
    // Each tile whose remembered ground went or came, for the ground's drawing.
    uint32_t revision = 0;
    std::vector<Physics::TerrainChange> log;
};

void Setup(entt::registry& registry);

// Once a tick, after the players' sight: pictures taken of buildings players
// stop seeing; pictures and remembered ground seen again forgotten.
void Update(entt::registry& registry);

// `thing`, a map object the fog keeps shown, is going -- cut down or used up:
// each player who has explored its tile and does not see it keeps a picture
// of it as it is now, unless it keeps one already.
void Going(entt::registry& registry, entt::entity thing);

// A building is about to lay new terrain on the tile: each player who has
// explored it and does not see it remembers it as `was`.
void GroundChanging(entt::registry& registry, int x, int y, Core::TerrainType was);

// The terrain `viewer` sees on the tile: what it remembers, or `now`.
Core::TerrainType GroundSeen(const entt::registry& registry, Core::PlayerId viewer, int x, int y,
                             Core::TerrainType now);

// The memory; null if the world was built without one.
const Memory* Read(const entt::registry& registry);

} // namespace ECS::Fog
