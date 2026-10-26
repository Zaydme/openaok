#include "Grid.h"

namespace Physics {

namespace {
uint32_t g_next_serial = 1;
} // namespace

Grid::Grid(int w, int h) : width(w), height(h), serial(g_next_serial++) {
    tiles.resize(static_cast<size_t>(width * height));
    out_of_bounds_tile.is_walkable = false;
    out_of_bounds_tile.terrain = Core::TerrainType::DeepWater;
}

void Grid::Resize(int w, int h) {
    width = w;
    height = h;
    for (uint32_t& revision : walk_revision) ++revision;
    tiles.clear();
    tiles.resize(static_cast<size_t>(width * height));
}


bool Grid::IsWalkable(int x, int y) const {
    if (!InBounds(x, y)) return false;
    return tiles[static_cast<size_t>(y * width + x)].is_walkable;
}

void Grid::SetWalkable(int x, int y, bool walkable) {
    if (!InBounds(x, y)) return;
    tiles[static_cast<size_t>(y * width + x)].blocked = !walkable;
    Refresh(x, y);
}

bool Grid::IsBlocked(int x, int y) const {
    if (!InBounds(x, y)) return true;
    return tiles[static_cast<size_t>(y * width + x)].blocked;
}

void Grid::SetLandPassability(const std::vector<bool>& by_terrain) {
    land_passable = by_terrain;
}

bool Grid::LandPassable(Core::TerrainType terrain) const {
    const size_t index = static_cast<size_t>(terrain);
    if (index < land_passable.size()) return land_passable[index];
    switch (terrain) {
    case Core::TerrainType::DeepWater:
    case Core::TerrainType::MediumWater:
    case Core::TerrainType::Swamp:
        return false;
    default:
        return true;
    }
}

void Grid::SetLayers(const std::vector<std::vector<bool>>& grounds,
                     const std::vector<int8_t>& layer_of_restriction) {
    layer_count = 1;
    layer_grounds.assign(1, {});
    if (!grounds.empty()) {
        land_passable = grounds[0];
        for (size_t l = 1; l < grounds.size() && l < static_cast<size_t>(kMaxLayers); ++l) {
            layer_grounds.push_back(grounds[l]);
            ++layer_count;
        }
    }
    restriction_layer = layer_of_restriction;
    for (int8_t& layer : restriction_layer) {
        if (layer >= layer_count) layer = -1;
    }
}

Layer Grid::LayerOf(int restriction) const {
    if (restriction < 0 || restriction >= static_cast<int>(restriction_layer.size())) return kLand;
    const int8_t layer = restriction_layer[static_cast<size_t>(restriction)];
    return layer > 0 ? static_cast<Layer>(layer) : kLand;
}

bool Grid::GroundPasses(Layer layer, Core::TerrainType terrain) const {
    layer = BaseLayer(layer);
    if (layer == kLand) return LandPassable(terrain);
    if (layer >= layer_count) return false;
    const auto& grounds = layer_grounds[layer];
    const size_t index = static_cast<size_t>(terrain);
    return index < grounds.size() && grounds[index];
}

bool Grid::Passable(Layer layer, int x, int y) const {
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

void Grid::SetGate(int x, int y, Core::PlayerId owner) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    const uint8_t gate = static_cast<uint8_t>(owner & kGateOwnerBits);
    if (tile.gate == gate) return;
    tile.gate = gate;
    // Regions run through a gate that is not locked: see Pathfinder.
    ++walk_revision[kLand];
}

void Grid::SetGateLocked(int x, int y, bool locked) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    if (tile.gate == 0) return;
    const uint8_t gate = static_cast<uint8_t>(locked ? (tile.gate | kGateLocked)
                                                     : (tile.gate & ~kGateLocked));
    if (gate == tile.gate) return;
    tile.gate = gate;
    ++walk_revision[kLand];
}

void Grid::SetGateFriends(Core::PlayerId owner, uint16_t friends) {
    if (owner >= 16) return;
    if (gate_friends[owner] == friends) return;
    gate_friends[owner] = friends;
    ++walk_revision[kLand];
}

bool Grid::GateOpensFor(int x, int y, Core::PlayerId player) const {
    if (!InBounds(x, y) || player == 0) return false;
    const Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    if (tile.gate == 0 || (tile.gate & kGateLocked) != 0) return false;
    if (!LandPassable(tile.terrain)) return false;
    const Core::PlayerId owner = static_cast<Core::PlayerId>(tile.gate & kGateOwnerBits);
    return owner == player || ((gate_friends[owner] >> player) & 1u) != 0;
}

uint8_t Grid::LayerBits(const Tile& tile) const {
    uint8_t bits = 0;
    if (tile.blocked) return bits;
    for (int l = 1; l < layer_count; ++l) {
        if (GroundPasses(static_cast<Layer>(l), tile.terrain)) bits |= static_cast<uint8_t>(1u << l);
    }
    return bits;
}

void Grid::StoreBits(Tile& tile, uint8_t bits) {
    const uint8_t changed = static_cast<uint8_t>(tile.passable ^ bits);
    if (changed == 0) return;
    for (int l = 1; l < kMaxLayers; ++l) {
        if ((changed >> l) & 1u) ++walk_revision[l];
    }
    tile.passable = bits;
}

void Grid::Refresh(int x, int y) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    const bool walkable = !tile.blocked && LandPassable(tile.terrain);
    if (walkable != tile.is_walkable) ++walk_revision[kLand];
    tile.is_walkable = walkable;
    StoreBits(tile, LayerBits(tile));
}

bool Grid::RestoreTiles(const std::vector<Tile>& saved) {
    if (saved.size() != tiles.size()) return false;
    tiles = saved;
    terrain_log.clear();
    WalkabilityChanged();
    return true;
}

void Grid::WalkabilityChanged() {
    ++walk_revision[kLand];
    for (Tile& tile : tiles) StoreBits(tile, LayerBits(tile));
    for (int l = 1; l < kMaxLayers; ++l) ++walk_revision[l];
}

void Grid::SetTerrain(int x, int y, Core::TerrainType terrain) {
    if (!InBounds(x, y)) return;
    Tile& tile = tiles[static_cast<size_t>(y * width + x)];
    if (tile.terrain == terrain) return;
    tile.terrain = terrain;
    const bool walkable = !tile.blocked && LandPassable(terrain);
    if (walkable != tile.is_walkable) ++walk_revision[kLand];
    tile.is_walkable = walkable;
    StoreBits(tile, LayerBits(tile));
    ++terrain_revision;
    if (terrain_log.size() >= kTerrainLogSize) {
        terrain_log.erase(terrain_log.begin(),
                          terrain_log.begin() + static_cast<long>(kTerrainLogSize / 2));
    }
    terrain_log.push_back(TerrainChange{terrain_revision, x, y});
}

const Tile& Grid::GetTile(int x, int y) const {
    if (!InBounds(x, y)) return out_of_bounds_tile;
    return tiles[static_cast<size_t>(y * width + x)];
}

Tile& Grid::GetTileMut(int x, int y) {
    return tiles[static_cast<size_t>(y * width + x)];
}

} // namespace Physics
