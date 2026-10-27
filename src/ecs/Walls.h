#pragma once
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/FixedPoint.h"
#include "core/Types.h"
#include "physics/Grid.h"

// Walls, gates, and the towers set in them.
//
// A wall is a line of one-tile buildings, dragged out from one tile to
// another and laid as foundations all at once, each charged its own price.
// Which piece each segment shows is not its own business: the dat marks walls,
// gates and towers "adjacent mode" (Building.AdjacentMode), meaning the
// buildings beside one choose which angle of its graphic it draws. A wall's
// graphic has five -- a run along the engine's y (the screen's "/"), a run
// along x ("\"), a post, and the two diagonals ("-" and "|"). A segment with
// one wall on each side of it along one line, and nothing else touching it,
// is that line's run; anything else -- an end, a corner, a junction, a lone
// segment -- is a post. A tower or a gate beside
// a wall counts like a wall, which is how a tower sits in a wall.
//
// A gate is built over a wall: the Gate button (GTAX2) turns to the wall it
// is put on -- GTAX2 along y, GTBX2 along x, GTCX2 and GTDX2 on the
// diagonals -- and takes the place of the wall segments under it. Finished, its
// tiles stay blocked, and the grid lets its owner's and allies' units through
// them (physics/Grid's keyed layers); locked, nobody.
namespace ECS::Walls {

// The angle of a wall's graphic each piece is stored at.
enum Piece : int {
    kAlongY = 0,
    kAlongX = 1,
    kPost = 2,
    kAcross = 3,   // the diagonal x+1, y-1: across the screen
    kDown = 4,     // the diagonal x+1, y+1: down the screen
};

// Whether the dat lets the buildings beside this one choose its piece:
// walls, gates and towers.
bool Joins(int unit_id);

// Whether the unit is a wall segment: adjacent mode, one tile, a wall's
// five-piece graphic (the dat's class 27).
bool IsWall(int unit_id);

// Whether the unit is a gate (the dat's class 39) -- any of its orientations,
// the button or what stands.
bool IsGate(int unit_id);

// The tiles a wall dragged from one tile to another is laid on, in order from
// the first: a straight line where the drag is along an axis or a diagonal,
// and the nearest staircase of tiles between them otherwise -- each tile
// touching the last, by a side or a corner (an integer Bresenham line).
std::vector<std::pair<int, int>> Line(int x0, int y0, int x1, int y1);

// Lays a line of `unit_id` foundations for `player` from one tile to the
// other: every tile of the line the wall can stand on gets one, charged at the
// player's price, in order, until the player can pay for no more. Tiles
// already holding one of the player's own segments are passed over, as is
// anything blocked. Returns the new sites, in line order.
std::vector<entt::entity> Lay(entt::registry& registry, Physics::Grid& grid,
                              Core::PlayerId player, int unit_id,
                              int x0, int y0, int x1, int y1);

// The piece a wall segment shows, from what joins it.
int PieceOf(const entt::registry& registry, entt::entity wall);

// Brings every joining building of `owner` on the tiles from (x0, y0) to
// (x1, y1) up to date with its neighbours: a finished one's art, a site's
// construction stages. After anything joining came or went beside them.
void Refresh(entt::registry& registry, Core::PlayerId owner, int x0, int y0, int x1, int y1);

// The same, around a building's own footprint.
void RefreshAround(entt::registry& registry, entt::entity building);

// The gate a Gate button puts up when placed at a tile: the orientation of
// the player's wall it is put on (the button's own when there is none).
// Returns the button unit of that orientation.
int GateFor(const entt::registry& registry, Core::PlayerId player, int button_unit,
            int tile_x, int tile_y);

// Whether a gate of `unit_id` can go up at the tile: every tile of it open
// ground it may stand on, or one of the player's own wall segments.
bool CanPlaceGate(const entt::registry& registry, const Physics::Grid& grid,
                  Core::PlayerId player, int unit_id, int tile_x, int tile_y);

// Whether a build order for the button `unit_id` at the tile finds its ground
// open: a gate, turned to the wall under it, on open ground or the player's
// own wall; anything else on open ground. The order and the interface both ask
// this, so a site the cursor shows open is one the order takes.
bool SiteOpen(const entt::registry& registry, const Physics::Grid& grid, Core::PlayerId player,
              int unit_id, int tile_x, int tile_y);

// Takes down the player's own wall segments that a gate of `unit_id` placed at
// the tile would stand on, so the gate can take their place.
void ClearForGate(entt::registry& registry, Physics::Grid& grid, Core::PlayerId player,
                  int unit_id, int tile_x, int tile_y);

// A gate finished, or going: its tiles become the owner's gate on the grid,
// or plain ground again.
void GateFinished(entt::registry& registry, Physics::Grid& grid, entt::entity gate);
void GateGone(entt::registry& registry, Physics::Grid& grid, entt::entity gate);

// Locks or unlocks a finished gate. Units standing in it when it locks step
// out of it. False if it is not a finished gate or is already so.
bool SetLocked(entt::registry& registry, Physics::Grid& grid, entt::entity gate, bool locked);

// Who besides each player its gates open for: its team-mates. Set at the
// start (and by diplomacy, when there is any).
void SetAllies(entt::registry& registry, Physics::Grid& grid);

// Composes a finished wall's or gate's art. Called by the buildings module in
// place of its ordinary composition; true if this was one.
bool SpawnArt(entt::registry& registry, entt::entity building);

// A wall site's construction stages: those of its own piece.
void SiteArt(entt::registry& registry, entt::entity site);

// Once a tick: each gate drawn raised while a unit it opens for is at it.
void Update(entt::registry& registry, const Physics::Grid& grid);

// What placing a wall or a gate from the build menu would put up, for the
// interface's preview: each piece's art, the tile it stands for, where it is
// drawn (in world units from that tile) and whether it can go there. A wall
// dragged from one tile to another is its whole line, each segment the piece
// the line gives it; a gate is its arch and posts, turned to the wall under it.
struct GhostPiece {
    int tile_x = 0;
    int tile_y = 0;
    Core::Fixed32 dx{};
    Core::Fixed32 dy{};
    int slp = -1;
    int frame = 0;
    bool clear = false;
};
std::vector<GhostPiece> Ghost(const entt::registry& registry, const Physics::Grid& grid,
                              Core::PlayerId player, int button_unit,
                              int from_x, int from_y, int to_x, int to_y);

} // namespace ECS::Walls
