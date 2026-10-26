#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "core/RMSParser.h"
#include "physics/Grid.h"

// Hills, from a random map script's <ELEVATION_GENERATION>.
//
// Each `create_elevation H` grows its clumps over the base terrain it names,
// then raises the ground inside them a level for every tile in from the
// clump's edge, up to H, so hills climb one level a tile and level off in
// plateaus. The start areas stay flat. Run on a random stream of its own, so
// adding hills changes nothing else the script places.
namespace Core::Elevation {

// The script's map size reference: "set_scale_by_size" and
// "set_scale_by_groups" scale against a 100 by 100 map.
constexpr int kReferenceTiles = 100 * 100;

void Generate(Physics::Grid& grid, const RMSScript& script, uint32_t seed,
              const std::vector<std::pair<int, int>>& starts, int start_radius);

} // namespace Core::Elevation
