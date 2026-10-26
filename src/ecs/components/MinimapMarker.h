#pragma once
#include <cstdint>

namespace ECS::Components {

// A dot the minimap draws for something nobody owns.
//
// Owned units and buildings are drawn in their player's colour, but the map's
// own contents have a colour each in the unit table: gold is a dull brown,
// stone grey, and every huntable and forage bush the same yellow-green. A
// colour of zero means the object is not shown at all, so trees and relics
// carry no marker.
struct MinimapMarker {
    uint8_t palette_index = 0;
};

} // namespace ECS::Components
