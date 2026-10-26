#include "ecs/BuildingGeometry.h"

#include <algorithm>

#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Rules.h"

namespace ECS::Buildings {

namespace {
// Half a tile: a tile is a unit across, centred on its integer coordinates.
const Core::Fixed32 kHalfTile = Core::Fixed32(1) / Core::Fixed32(2);
} // namespace

Core::Fixed32 Bounds::DistanceFrom(Components::Vector2D point) const {
    const Core::Fixed32 zero(0);
    const Core::Fixed32 dx = Core::FixedMath::Max(
        Core::FixedMath::Max(min_x - point.x, point.x - max_x), zero);
    const Core::Fixed32 dy = Core::FixedMath::Max(
        Core::FixedMath::Max(min_y - point.y, point.y - max_y), zero);
    return Core::FixedMath::Distance(dx, dy);
}

Bounds TileBounds(Components::Vector2D origin) {
    return {origin.x - kHalfTile, origin.y - kHalfTile,
            origin.x + kHalfTile, origin.y + kHalfTile};
}

namespace {

TileSpan SpanOf(int size) {
    const int low = -(size / 2);
    return {low, low + size - 1};
}

// Tiles along one of the dat's axes, from its half-extent there.
int TilesFor(Core::Fixed32 half_extent) {
    return std::max(1, (half_extent * Core::Fixed32(2) + kHalfTile).ToInt());
}

} // namespace

Footprint FootprintOf(int unit_id) {
    const Core::Rules::UnitRules* rules = Core::Rules::Unit(unit_id);
    if (rules != nullptr && (rules->clearance_x.value > 0 || rules->clearance_y.value > 0)) {
        // The dat's x extent is the engine's y extent (see Footprint).
        return {SpanOf(TilesFor(rules->clearance_y)), SpanOf(TilesFor(rules->clearance_x))};
    }
    const auto* ui = Core::ConfigManager::Get().GetUnitInterface(unit_id);
    const int size = (ui && ui->footprint > 0) ? ui->footprint : 1;
    return {SpanOf(size), SpanOf(size)};
}

int FootprintTiles(int unit_id) {
    const Footprint f = FootprintOf(unit_id);
    return std::max(f.x.high - f.x.low + 1, f.y.high - f.y.low + 1);
}

TileSpan FootprintTileSpan(int unit_id) {
    return FootprintOf(unit_id).x;
}

Components::Vector2D FromDatAxes(Core::Fixed32 dx, Core::Fixed32 dy) {
    return {dy, Core::Fixed32(0) - dx};
}

Bounds FootprintBounds(Components::Vector2D origin, int unit_id) {
    const Footprint f = FootprintOf(unit_id);
    return {origin.x + Core::Fixed32(f.x.low) - kHalfTile,
            origin.y + Core::Fixed32(f.y.low) - kHalfTile,
            origin.x + Core::Fixed32(f.x.high) + kHalfTile,
            origin.y + Core::Fixed32(f.y.high) + kHalfTile};
}

void ForEachFootprintTile(int unit_id, int tile_x, int tile_y,
                          const std::function<void(int, int)>& visit) {
    const Footprint f = FootprintOf(unit_id);
    for (int dy = f.y.low; dy <= f.y.high; ++dy) {
        for (int dx = f.x.low; dx <= f.x.high; ++dx) {
            visit(tile_x + dx, tile_y + dy);
        }
    }
}

} // namespace ECS::Buildings
