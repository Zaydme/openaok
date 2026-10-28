#pragma once
#include <entt/entt.hpp>

#include "ai/Interpreter.h"
#include "ecs/components/Transform.h"
#include "physics/Grid.h"

// Where a computer player puts a building for `build`.
//
// By what the building is for, as the data says:
//
//   - a drop site for wood: at the edge of the nearest forest it has seen,
//     within sn-camp-max-distance of home, where no drop site already is;
//   - a drop site for gold or stone: beside the nearest mine likewise --
//     gold first, then stone;
//   - the food drop site (the Mill): beside forage, within
//     sn-mill-max-distance; failing that, near home, for farms;
//   - a farm: packed against the Town Center, then against a Mill;
//   - a Dock: on the shore of water of at least
//     sn-minimum-water-body-size-for-dock tiles;
//   - anything else: in town, within sn-maximum-town-size of home --
//     or, for build-forward, toward the nearest enemy building.
//
// Every spot is the nearest open ground to its anchor searched ring by ring
// in the same order every time, with a tile clear all round so a town never
// walls its own villagers in.
namespace AI::Placement {

// Ground already spoken for: the footprints of the player's own buildings,
// foundations and farms among them -- which do not block the grid, so the
// grid alone would let a second farm be laid on the first.
struct Taken {
    struct Rect {
        int x0, y0, x1, y1;
    };
    std::vector<Rect> rects;
    bool Overlaps(int unit_id, int x, int y) const;
};
Taken TakenBy(const entt::registry& registry, const Census& census);

// A tile for `player` to place `unit_id` on, between `near` and `far` tiles
// from `anchor`, that its build order would take (Buildings::Check: ground it
// has explored, clear of its side's foundations and its enemies' units).
// `breathe`: keep a clear tile all round it.
bool Find(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
          int unit_id, const ECS::Components::Vector2D& anchor, int near, int far, int& tile_x,
          int& tile_y, bool breathe = true, const Taken* taken = nullptr);

// A tile for `unit_id` with its footprint touching `building`'s, nearest
// the building's middle first, that `player`'s build order would take.
bool Hug(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
         int unit_id, entt::entity building, int& tile_x, int& tile_y, const Taken* taken = nullptr);

// Where the player's `build` (or `build-forward`) of `slot` goes.
bool Site(Turn& turn, int slot, bool forward, int& tile_x, int& tile_y);

} // namespace AI::Placement
