#pragma once
#include <SDL2/SDL.h>

#include "ecs/Simulation.h"

namespace Rendering {

class Renderer;

// The fog of war over the ground, for the player at this machine.
//
// * Each tile has one of three states: seen now, explored but not seen, never
//   explored. A never-explored tile is not drawn at all, so it stays black.
//   With the map revealed, nothing counts as unexplored.
// * Two neighbour patterns are taken for a tile: which of its eight
//   neighbours are not seen now, and which are not explored. Bits:
//   0x01 left corner, 0x02 bottom corner, 0x04 right corner, 0x08 top corner,
//   0x10 lower-left edge, 0x20 lower-right edge, 0x40 upper-right edge,
//   0x80 upper-left edge. Off the map an edge neighbour counts as unexplored
//   but as seen; a corner off the map counts as neither. The 256 patterns fold
//   onto 47 (a corner beside a set edge adds nothing), numbered in order of
//   first appearance -- BuildPatternTable.
// * TileEdge.Dat holds, per pattern k, two masks that split the tile exactly
//   in two: TileEdge[2k] is the part drawn clear, TileEdge[2k+1] the part drawn
//   dithered. A seen tile takes the pair for its not-seen pattern; a fogged
//   tile is all dithered, TileEdge[0].
// * BlkEdge.Dat holds, per not-explored pattern k, the band left black
//   between explored ground and unexplored: it is cut out of both parts,
//   always inside the dithered part.
// * The dither turns every other pixel black (palette 0) in a one-pixel
//   checkerboard fixed to the screen, not to the tile: the pixels with x + y
//   even.
// * Each file holds 17 mask sets, for flat ground and 16 slopes. Only the
//   flat set is used, laid onto each tile's own corners exactly as the
//   terrain is (TerrainRenderer's hill fan).
//
// Objects:
//
// * Everything the dat puts on layer 5 or under -- farms, rubble, cliffs and
//   most shadows -- is drawn before all else, fogged exactly as the ground
//   under it is: clear, dithered, or not at all where the ground is black.
// * Everything else is drawn whole, over the fogged ground: clear if its tile
//   is seen, even where it reaches over fog or black (a tree at the edge of
//   sight stands over the black), and dithered all over with the same screen
//   checkerboard if its tile is only explored. Which objects show in the fog
//   at all is ECS::Vision::Shows.
//
// Drawn here into a frame-sized layer: the dithered shapes as solid black,
// then the screen checkerboard multiplied into their alpha, then the black
// shapes, and the layer laid over the frame. Masks are baked into one atlas
// once; each frame is two batches of triangles. Objects in the fog use the
// same layer: their silhouettes in black, wiped where a seen object stands in
// front, then the same checkerboard.
class FogRenderer {
public:
    // After the terrain and the ground-level objects, before anything
    // standing on the ground.
    static void Draw(ECS::Simulation& sim, Renderer& renderer, SDL_Renderer* sdl_r);

    // Dithering whole objects: between these, draw (at the world's scale) the
    // silhouette in black of every object in the fog, and wipe with
    // EraseBlend() whatever is drawn in front of one; End lays the
    // checkerboard of it over the frame. False if the layer cannot be had,
    // and then End must not be called.
    static bool BeginObjects(Renderer& renderer, SDL_Renderer* sdl_r);
    static void EndObjects(Renderer& renderer, SDL_Renderer* sdl_r);
    // Takes away the layer's coverage where the source is opaque.
    static SDL_BlendMode EraseBlend();
};

} // namespace Rendering
