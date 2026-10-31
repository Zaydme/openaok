#pragma once
#include <vector>
#include <cstdint>
#include "core/FixedPoint.h"
#include "core/Types.h"

namespace Physics {

// The tile a world position is on. Tiles are a unit across and centred on
// their whole-number coordinates, so a position rounds to the nearest whole
// number.
inline int TileOf(Core::Fixed32 v) {
    return (v + Core::Fixed32(1) / Core::Fixed32(2)).ToInt();
}

// Which kind of mover a question about passable ground is asked for.
//
// The dat gives every unit a terrain restriction: per terrain, whether a unit
// of it may go there. Each distinct restriction the movers in the data use is
// a layer, with its own walkable tiles and connected regions. Restrictions
// that allow the same grounds share a layer: a fishing ship, a galley and a
// transport are three restrictions in the dat and one sea.
//
// Layer 0 is always land, the villager's: everything that asks about "walkable"
// ground without saying for whom asks about it.
using Layer = uint8_t;
constexpr Layer kLand = 0;
constexpr int kMaxLayers = 8;

// A layer can also say who is asking, for gates: open to their owner and
// allies, shut to everyone else. The low three bits are the layer, the next
// four the player whose key it carries (0: nobody's). ecs/Navigation hands
// every mover the layer keyed with its owner; anything that asks without a
// key (placing a building, spawning a unit) sees a gate as a wall.
constexpr Layer kLayerBits = 0x07;
constexpr int kKeyShift = 3;
constexpr Layer BaseLayer(Layer layer) { return static_cast<Layer>(layer & kLayerBits); }
constexpr Core::PlayerId KeyOf(Layer layer) {
    return static_cast<Core::PlayerId>(layer >> kKeyShift);
}
constexpr Layer Keyed(Layer layer, Core::PlayerId player) {
    return static_cast<Layer>(BaseLayer(layer) | (player << kKeyShift));
}

// A gate on a tile: its owner in the low bits, and whether it is locked.
constexpr uint8_t kGateOwnerBits = 0x0F;
constexpr uint8_t kGateLocked = 0x80;

// Two separate facts make a tile passable: its ground lets the layer's movers
// on (grass and beach let land units on, water does not), and nothing stands
// on it. They are stored apart because they change apart: a felled tree frees
// its tile; a dock stands on water.
struct Tile {
    Core::TerrainType terrain = Core::TerrainType::Grass1;
    uint8_t texture_variant = 0;
    int elevation = 0;
    // Land units may be here: the ground allows it and nothing blocks it.
    bool is_walkable = true;
    // Something stands here -- a tree, a mine, a building.
    bool blocked = false;
    bool has_tree = false;
    // The same for every other layer, a bit each (bit n: layer n). Land keeps
    // its own flag above, which the map generator writes directly.
    uint8_t passable = 0;
    // A gate stands here (0: none): its owner, and kGateLocked when locked.
    // The tile is blocked like any building's; a keyed land layer finds it
    // open when the gate opens for that key.
    uint8_t gate = 0;
};

// One change to a tile's ground during play: a farm planted, a foundation
// laid. Stamped with the grid's terrain revision when it happened.
struct TerrainChange {
    uint32_t revision = 0;
    int x = 0;
    int y = 0;
};

class Grid {
public:
    Grid(int width, int height);
    void Resize(int w, int h);
    // Recomputes a tile's passability, for every layer, from its ground and
    // what stands on it.
    void Refresh(int x, int y);


    bool IsWalkable(int x, int y) const;
    // Marks something as standing on the tile (false), or the tile as clear
    // of it (true).
    // Whether land units may then walk it is also up to its ground.
    void SetWalkable(int x, int y, bool walkable);
    bool IsBlocked(int x, int y) const;

    // Which grounds land units may cross, by terrain: the dat's land-unit
    // terrain restriction, set once the rules are loaded. Every tile's
    // walkability follows from it and from what stands there.
    void SetLandPassability(const std::vector<bool>& by_terrain);
    bool LandPassable(Core::TerrainType terrain) const;

    // The layers: for each, which grounds its movers may cross (by terrain),
    // and which layer each of the dat's terrain restrictions moves on (-1: not
    // a mover's). Layer 0's grounds are land's. Set once the rules are loaded,
    // before the map is written; every tile's passability follows.
    void SetLayers(const std::vector<std::vector<bool>>& grounds,
                   const std::vector<int8_t>& layer_of_restriction);
    int LayerCount() const { return layer_count; }
    // The layer a unit of terrain restriction `restriction` moves on. Land
    // for anything the layers were not set up for.
    Layer LayerOf(int restriction) const;
    // Whether the layer's movers may be on the tile: its ground lets them and
    // nothing stands there. Layer 0 is IsWalkable.
    bool Passable(Layer layer, int x, int y) const;
    // Whether the layer's movers may cross a ground at all, whatever stands
    // on it.
    bool GroundPasses(Layer layer, Core::TerrainType terrain) const;

    // Gates. A gate's tiles are blocked; `SetGate` marks them its owner's
    // (0 clears them) and `SetGateLocked` locks or unlocks them. Who a
    // player's unlocked gates open for besides the player is its allies,
    // a bit per player -- the simulation sets it from the lobby's teams.
    void SetGate(int x, int y, Core::PlayerId owner);
    void SetGateLocked(int x, int y, bool locked);
    void SetGateFriends(Core::PlayerId owner, uint16_t friends);
    // Whether the gate on (x, y), if any, opens for `player`: unlocked, and
    // the player is its owner or an ally of its owner.
    bool GateOpensFor(int x, int y, Core::PlayerId player) const;

    const Tile& GetTile(int x, int y) const;
    Tile& GetTileMut(int x, int y);

    // Changes a tile's ground during play and records it, so whatever has
    // drawn the ground can tell what is out of date without being told.
    void SetTerrain(int x, int y, Core::TerrainType terrain);
    // Bumped by every SetTerrain. Map generation writes tiles directly and
    // leaves it at zero.
    uint32_t TerrainRevision() const { return terrain_revision; }
    // The most recent changes, oldest first, at most kTerrainLogSize of them.
    // A reader that has fallen further behind than that redraws everything.
    static constexpr size_t kTerrainLogSize = 1024;
    const std::vector<TerrainChange>& TerrainLog() const { return terrain_log; }

    // Which grid this is, and how many times a layer's passability has
    // changed: together they say whether something worked out from it -- the
    // pathfinder's regions -- is still true. Each layer counts its own, so a
    // tree felled on land leaves the sea's regions standing.
    uint32_t Serial() const { return serial; }
    uint32_t WalkRevision(Layer layer = kLand) const {
        return walk_revision[BaseLayer(layer)];
    }
    // Map generation writes tiles directly; once it has, it says so, and
    // every layer is worked out again from the ground and what stands on it.
    void WalkabilityChanged();

    // Every tile as it stands, and putting a saved set back: the ground and
    // what stands on it, with every layer worked out again from them.
    const std::vector<Tile>& Tiles() const { return tiles; }
    bool RestoreTiles(const std::vector<Tile>& saved);

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }

    bool InBounds(int x, int y) const { return x >= 0 && x < width && y >= 0 && y < height; }

private:
    // The bits of every layer past land, for a tile as it stands.
    uint8_t LayerBits(const Tile& tile) const;
    // Stores a tile's new layer bits, bumping each layer that changed.
    void StoreBits(Tile& tile, uint8_t bits);

    int width;
    int height;
    std::vector<Tile> tiles;
    Tile out_of_bounds_tile;
    std::vector<bool> land_passable;   // by TerrainType; empty: water refused
    // Layers past land: grounds by terrain, per layer (index 0 unused --
    // land is `land_passable`).
    std::vector<std::vector<bool>> layer_grounds;
    std::vector<int8_t> restriction_layer;
    int layer_count = 1;
    // Per gate owner, who besides the owner its unlocked gates open for.
    uint16_t gate_friends[16] = {};
    uint32_t terrain_revision = 0;
    uint32_t serial = 0;
    uint32_t walk_revision[kMaxLayers] = {};
    std::vector<TerrainChange> terrain_log;
};

// The two questions route searches ask of every tile they look at, inline.
inline bool Grid::IsWalkable(int x, int y) const {
    if (!InBounds(x, y)) return false;
    return tiles[static_cast<size_t>(y * width + x)].is_walkable;
}

inline bool Grid::Passable(Layer layer, int x, int y) const {
    const Layer base = BaseLayer(layer);
    if (base == kLand) {
        if (IsWalkable(x, y)) return true;
        // A gate is land, open to the key it opens for.
        const Core::PlayerId key = KeyOf(layer);
        return key != 0 && GateOpensFor(x, y, key);
    }
    if (!InBounds(x, y)) return false;
    return (tiles[static_cast<size_t>(y * width + x)].passable >> base) & 1u;
}

} // namespace Physics
